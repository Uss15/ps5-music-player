#include "audio.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
#include "../third_party/minimp3/minimp3_ex.h"

#define OUTPUT_RATE 48000u
#define OUTPUT_CHANNELS 2u
#define OUTPUT_FRAMES 1024u
#define MAX_DECODED_BYTES (128u * 1024u * 1024u)
#define MAX_COMPRESSED_BYTES (32u * 1024u * 1024u)
#define AUDIO_PORT_BGM 1
#define AUDIO_PARAM_S16_STEREO 1

typedef struct {
    int16_t *samples;
    size_t sample_count; /* Interleaved scalar samples, not frames. */
    unsigned channels;
    unsigned sample_rate;
} decoded_audio_t;

/* Prototypes exported by PS5 Payload SDK libSceAudioOut. */
extern int32_t sceAudioOutInit(void);
extern int32_t sceAudioOutOpen(int32_t user_id, int32_t port_type, int32_t index,
                               uint32_t length, uint32_t sample_rate, uint32_t param_type);
extern int32_t sceAudioOutOutput(int32_t handle, const void *samples);
extern int32_t sceAudioOutClose(int32_t handle);

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static pthread_t worker;
static int worker_started;
static int stopping;
static int pending;
static int paused;
static int ready;
static int output_handle = -1;
static int requested_index = -1;
static int current_index = -1;
static int volume_percent = 80;
static uint64_t position_ms;
static uint64_t duration_ms;
static audio_state_t state = AUDIO_STATE_UNAVAILABLE;
static char requested_path[AUDIO_PATH_CAP];
static char last_error[128];
static int16_t output_buffer[OUTPUT_FRAMES * OUTPUT_CHANNELS];

static uint16_t read_le16(const unsigned char *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int load_wav(const char *path, decoded_audio_t *out) {
    FILE *file = fopen(path, "rb");
    if (!file) return -1;
    unsigned char header[12];
    if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
        memcmp(header, "RIFF", 4) || memcmp(header + 8, "WAVE", 4)) {
        fclose(file);
        return -1;
    }

    unsigned format = 0, channels = 0, sample_rate = 0, bits = 0, block_align = 0;
    long data_offset = -1;
    uint32_t data_size = 0;
    unsigned char chunk[8];
    while (fread(chunk, 1, sizeof(chunk), file) == sizeof(chunk)) {
        uint32_t size = read_le32(chunk + 4);
        long start = ftell(file);
        if (start < 0) break;
        if (!memcmp(chunk, "fmt ", 4)) {
            unsigned char fmt[16];
            if (size < sizeof(fmt) || fread(fmt, 1, sizeof(fmt), file) != sizeof(fmt)) break;
            format = read_le16(fmt);
            channels = read_le16(fmt + 2);
            sample_rate = read_le32(fmt + 4);
            block_align = read_le16(fmt + 12);
            bits = read_le16(fmt + 14);
        } else if (!memcmp(chunk, "data", 4)) {
            data_offset = start;
            data_size = size;
        }
        if (fseek(file, start + (long)size + (long)(size & 1u), SEEK_SET) != 0) break;
        if (format && data_offset >= 0) break;
    }
    if (format != 1 || (channels != 1 && channels != 2) ||
        (bits != 8 && bits != 16) || !sample_rate || data_offset < 0 ||
        block_align != channels * (bits / 8) || !block_align || data_size < block_align ||
        (uint64_t)data_size > MAX_DECODED_BYTES) {
        fclose(file);
        return -1;
    }
    size_t frames = data_size / block_align;
    size_t count = frames * channels;
    int16_t *samples = malloc(count * sizeof(*samples));
    if (!samples || fseek(file, data_offset, SEEK_SET) != 0) {
        free(samples);
        fclose(file);
        return -1;
    }
    if (bits == 16) {
        unsigned char pair[2];
        for (size_t i = 0; i < count; ++i) {
            if (fread(pair, 1, 2, file) != 2) { free(samples); fclose(file); return -1; }
            samples[i] = (int16_t)read_le16(pair);
        }
    } else {
        for (size_t i = 0; i < count; ++i) {
            int sample = fgetc(file);
            if (sample == EOF) { free(samples); fclose(file); return -1; }
            samples[i] = (int16_t)((sample - 128) << 8);
        }
    }
    fclose(file);
    out->samples = samples;
    out->sample_count = count;
    out->channels = channels;
    out->sample_rate = sample_rate;
    return 0;
}

static int load_mp3(const char *path, decoded_audio_t *out) {
    struct stat st;
    if (stat(path, &st) != 0 || st.st_size <= 0 || (uint64_t)st.st_size > MAX_COMPRESSED_BYTES) return -1;
    mp3dec_t decoder;
    mp3dec_file_info_t info;
    memset(&info, 0, sizeof(info));
    if (mp3dec_load(&decoder, path, &info, NULL, NULL) != 0 || !info.buffer ||
        (info.channels != 1 && info.channels != 2) || info.hz <= 0 ||
        info.samples == 0 || info.samples > MAX_DECODED_BYTES / sizeof(int16_t)) {
        free(info.buffer);
        return -1;
    }
    out->samples = (int16_t *)info.buffer;
    out->sample_count = info.samples;
    out->channels = (unsigned)info.channels;
    out->sample_rate = (unsigned)info.hz;
    return 0;
}

static int decode_file(const char *path, decoded_audio_t *out) {
    memset(out, 0, sizeof(*out));
    const char *ext = strrchr(path, '.');
    if (!ext) return -1;
    if (!strcasecmp(ext, ".wav")) return load_wav(path, out);
    if (!strcasecmp(ext, ".mp3")) return load_mp3(path, out);
    return -1;
}

static void set_error(const char *message) {
    pthread_mutex_lock(&lock);
    snprintf(last_error, sizeof(last_error), "%s", message);
    state = AUDIO_STATE_ERROR;
    pthread_mutex_unlock(&lock);
}

static void *playback_worker(void *unused) {
    (void)unused;
    for (;;) {
        char path[AUDIO_PATH_CAP];
        int index;
        pthread_mutex_lock(&lock);
        while (!pending && !stopping) pthread_cond_wait(&changed, &lock);
        if (stopping) { pthread_mutex_unlock(&lock); break; }
        index = requested_index;
        snprintf(path, sizeof(path), "%s", requested_path);
        pending = 0;
        paused = 0;
        if (index < 0) {
            state = AUDIO_STATE_STOPPED;
            position_ms = 0;
            duration_ms = 0;
            last_error[0] = '\0';
            pthread_mutex_unlock(&lock);
            continue;
        }
        current_index = index;
        position_ms = 0;
        duration_ms = 0;
        last_error[0] = '\0';
        state = AUDIO_STATE_LOADING;
        pthread_mutex_unlock(&lock);

        decoded_audio_t decoded;
        if (decode_file(path, &decoded) != 0) {
            set_error("Could not decode this file (PS5 build supports PCM WAV and MP3).");
            continue;
        }
        size_t source_frames = decoded.sample_count / decoded.channels;
        double source_position = 0.0;
        uint64_t total_ms = (uint64_t)((source_frames * 1000ull) / decoded.sample_rate);
        pthread_mutex_lock(&lock);
        if (pending || stopping) {
            pthread_mutex_unlock(&lock);
            free(decoded.samples);
            continue;
        }
        duration_ms = total_ms;
        state = AUDIO_STATE_PLAYING;
        pthread_mutex_unlock(&lock);

        int failed = 0;
        while (source_position < (double)source_frames) {
            pthread_mutex_lock(&lock);
            while (paused && !pending && !stopping) {
                state = AUDIO_STATE_PAUSED;
                pthread_cond_wait(&changed, &lock);
            }
            if (stopping || pending) { pthread_mutex_unlock(&lock); break; }
            state = AUDIO_STATE_PLAYING;
            int vol = volume_percent;
            pthread_mutex_unlock(&lock);

            unsigned produced = 0;
            for (; produced < OUTPUT_FRAMES && source_position < (double)source_frames; ++produced) {
                size_t frame = (size_t)source_position;
                size_t next = frame + 1 < source_frames ? frame + 1 : frame;
                double fraction = source_position - (double)frame;
                int32_t left_a = decoded.samples[frame * decoded.channels];
                int32_t left_b = decoded.samples[next * decoded.channels];
                int32_t right_a = decoded.channels == 2 ? decoded.samples[frame * 2 + 1] : left_a;
                int32_t right_b = decoded.channels == 2 ? decoded.samples[next * 2 + 1] : left_b;
                int32_t left = left_a + (int32_t)((left_b - left_a) * fraction);
                int32_t right = right_a + (int32_t)((right_b - right_a) * fraction);
                left = (left * vol) / 100;
                right = (right * vol) / 100;
                output_buffer[produced * 2] = (int16_t)left;
                output_buffer[produced * 2 + 1] = (int16_t)right;
                source_position += (double)decoded.sample_rate / OUTPUT_RATE;
            }
            for (unsigned i = produced; i < OUTPUT_FRAMES; ++i) {
                output_buffer[i * 2] = 0;
                output_buffer[i * 2 + 1] = 0;
            }
            if (sceAudioOutOutput(output_handle, output_buffer) < 0) {
                failed = 1;
                break;
            }
            pthread_mutex_lock(&lock);
            position_ms = (uint64_t)(source_position * 1000.0 / decoded.sample_rate);
            pthread_mutex_unlock(&lock);
        }
        free(decoded.samples);
        pthread_mutex_lock(&lock);
        if (failed) {
            snprintf(last_error, sizeof(last_error), "AudioOut rejected the PCM buffer.");
            state = AUDIO_STATE_ERROR;
        } else if (!pending && !stopping) {
            state = AUDIO_STATE_STOPPED;
            position_ms = duration_ms;
        }
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

int audio_init(void) {
    pthread_mutex_lock(&lock);
    if (ready) { pthread_mutex_unlock(&lock); return 0; }
    pthread_mutex_unlock(&lock);
    if (sceAudioOutInit() != 0) {
        pthread_mutex_lock(&lock);
        state = AUDIO_STATE_UNAVAILABLE;
        snprintf(last_error, sizeof(last_error), "sceAudioOutInit failed.");
        pthread_mutex_unlock(&lock);
        return -1;
    }
    /* Port 1 is the PS5 BGM port; actual in-game mixing still needs console validation. */
    int32_t handle = sceAudioOutOpen(0xFF, AUDIO_PORT_BGM, 0, OUTPUT_FRAMES, OUTPUT_RATE, AUDIO_PARAM_S16_STEREO);
    if (handle <= 0) {
        pthread_mutex_lock(&lock);
        state = AUDIO_STATE_UNAVAILABLE;
        snprintf(last_error, sizeof(last_error), "AudioOutOpen failed (%d).", handle);
        pthread_mutex_unlock(&lock);
        return -1;
    }
    pthread_mutex_lock(&lock);
    output_handle = handle;
    stopping = 0;
    if (pthread_create(&worker, NULL, playback_worker, NULL) != 0) {
        sceAudioOutClose(output_handle);
        output_handle = -1;
        state = AUDIO_STATE_UNAVAILABLE;
        snprintf(last_error, sizeof(last_error), "Could not create audio worker thread.");
        pthread_mutex_unlock(&lock);
        return -1;
    }
    worker_started = 1;
    ready = 1;
    state = AUDIO_STATE_STOPPED;
    last_error[0] = '\0';
    pthread_mutex_unlock(&lock);
    return 0;
}

void audio_shutdown(void) {
    pthread_mutex_lock(&lock);
    stopping = 1;
    pthread_cond_broadcast(&changed);
    int join = worker_started;
    pthread_mutex_unlock(&lock);
    if (join) pthread_join(worker, NULL);
    pthread_mutex_lock(&lock);
    if (output_handle >= 0) sceAudioOutClose(output_handle);
    output_handle = -1;
    worker_started = 0;
    ready = 0;
    state = AUDIO_STATE_UNAVAILABLE;
    pthread_mutex_unlock(&lock);
}

int audio_ready(void) {
    pthread_mutex_lock(&lock);
    int result = ready;
    pthread_mutex_unlock(&lock);
    return result;
}

int audio_play(int track_index, const char *path) {
    if (!path || track_index < 1 || strlen(path) >= sizeof(requested_path)) return -1;
    pthread_mutex_lock(&lock);
    if (!ready) { pthread_mutex_unlock(&lock); return -1; }
    requested_index = track_index;
    snprintf(requested_path, sizeof(requested_path), "%s", path);
    pending = 1;
    paused = 0;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
    return 0;
}

int audio_pause(void) {
    pthread_mutex_lock(&lock);
    if (!ready || (state != AUDIO_STATE_PLAYING && state != AUDIO_STATE_LOADING)) { pthread_mutex_unlock(&lock); return -1; }
    paused = 1;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
    return 0;
}

int audio_resume(void) {
    pthread_mutex_lock(&lock);
    if (!ready || state != AUDIO_STATE_PAUSED) { pthread_mutex_unlock(&lock); return -1; }
    paused = 0;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
    return 0;
}

int audio_stop(void) {
    pthread_mutex_lock(&lock);
    if (!ready) { pthread_mutex_unlock(&lock); return -1; }
    requested_index = -1;
    requested_path[0] = '\0';
    pending = 1;
    paused = 0;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
    return 0;
}

int audio_set_volume(int volume) {
    if (volume < 0 || volume > 100) return -1;
    pthread_mutex_lock(&lock);
    volume_percent = volume;
    pthread_mutex_unlock(&lock);
    return 0;
}

void audio_get_snapshot(audio_snapshot_t *out) {
    if (!out) return;
    pthread_mutex_lock(&lock);
    out->state = state;
    out->track_index = current_index;
    out->volume = volume_percent;
    out->position_ms = position_ms;
    out->duration_ms = duration_ms;
    snprintf(out->error, sizeof(out->error), "%s", last_error);
    pthread_mutex_unlock(&lock);
}
