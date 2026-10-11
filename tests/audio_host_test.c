#include "../payload/audio.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static atomic_int output_calls;
static atomic_int non_silent_calls;

int32_t sceAudioOutInit(void) { return 0; }

int32_t sceAudioOutOpen(int32_t user_id, int32_t port_type, int32_t index,
                        uint32_t length, uint32_t sample_rate, uint32_t param_type) {
    (void)user_id; (void)port_type; (void)index;
    if (length != 1024 || sample_rate != 48000 || param_type != 1) return -1;
    return 17;
}

int32_t sceAudioOutOutput(int32_t handle, const void *data) {
    if (handle != 17 || !data) return -1;
    const int16_t *samples = data;
    for (size_t i = 0; i < 1024u * 2u; ++i) {
        if (samples[i] != 0) { atomic_fetch_add(&non_silent_calls, 1); break; }
    }
    atomic_fetch_add(&output_calls, 1);
    struct timespec delay = {.tv_sec = 0, .tv_nsec = 8000000};
    nanosleep(&delay, NULL);
    return 0;
}

int32_t sceAudioOutClose(int32_t handle) { return handle == 17 ? 0 : -1; }

static void put16(FILE *f, uint16_t value) {
    fputc(value & 0xff, f); fputc((value >> 8) & 0xff, f);
}
static void put32(FILE *f, uint32_t value) {
    put16(f, (uint16_t)(value & 0xffff)); put16(f, (uint16_t)(value >> 16));
}

static int make_test_wav(const char *path) {
    const uint32_t rate = 48000, frames = 48000, data_bytes = frames * 2 * 2;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite("RIFF", 1, 4, f); put32(f, 36 + data_bytes); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 2); put32(f, rate); put32(f, rate * 4); put16(f, 4); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, data_bytes);
    for (uint32_t i = 0; i < frames; ++i) {
        int16_t sample = (int16_t)(((i * 37u) % 20000u) - 10000);
        put16(f, (uint16_t)sample); put16(f, (uint16_t)sample);
    }
    int ok = fclose(f) == 0;
    return ok ? 0 : -1;
}

static audio_snapshot_t snapshot(void) {
    audio_snapshot_t s;
    memset(&s, 0, sizeof(s));
    audio_get_snapshot(&s);
    return s;
}

static int wait_for_state(audio_state_t target, int timeout_ms) {
    for (int i = 0; i < timeout_ms / 10; ++i) {
        if (snapshot().state == target) return 0;
        usleep(10000);
    }
    return -1;
}

int main(void) {
    char path[] = "/tmp/ps5-music-audio-test.wav";
    if (make_test_wav(path) != 0) { perror("create wav"); return 1; }
    if (audio_init() != 0 || !audio_ready()) { fprintf(stderr, "audio_init failed\n"); return 2; }
    if (audio_play(1, path) != 0) { fprintf(stderr, "audio_play failed\n"); return 3; }
    for (int i = 0; i < 200 && atomic_load(&output_calls) < 2; ++i) usleep(10000);
    if (atomic_load(&output_calls) < 2 || atomic_load(&non_silent_calls) == 0) {
        fprintf(stderr, "no decoded PCM reached AudioOut\n"); return 4;
    }
    if (audio_pause() != 0 || wait_for_state(AUDIO_STATE_PAUSED, 1000) != 0) {
        fprintf(stderr, "pause did not take effect\n"); return 5;
    }
    if (audio_set_volume(35) != 0 || snapshot().volume != 35) {
        fprintf(stderr, "volume state failed\n"); return 6;
    }
    if (audio_resume() != 0) { fprintf(stderr, "resume failed\n"); return 7; }
    if (audio_stop() != 0 || wait_for_state(AUDIO_STATE_STOPPED, 1000) != 0) {
        fprintf(stderr, "stop did not take effect\n"); return 8;
    }
    audio_shutdown();
    unlink(path);
    puts("host audio pipeline test: PASS (WAV decode, resample/output, pause/resume/stop, volume state)");
    return 0;
}
