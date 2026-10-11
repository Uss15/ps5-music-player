#ifndef PS5_MUSIC_AUDIO_H
#define PS5_MUSIC_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#define AUDIO_PATH_CAP 1024

typedef enum {
    AUDIO_STATE_UNAVAILABLE = 0,
    AUDIO_STATE_STOPPED,
    AUDIO_STATE_LOADING,
    AUDIO_STATE_PLAYING,
    AUDIO_STATE_PAUSED,
    AUDIO_STATE_ERROR
} audio_state_t;

typedef struct {
    audio_state_t state;
    int track_index;
    int volume;
    uint64_t position_ms;
    uint64_t duration_ms;
    char error[128];
} audio_snapshot_t;

/* Opens a 48 kHz signed-16 stereo BGM output and starts the playback worker. */
int audio_init(void);
void audio_shutdown(void);
int audio_ready(void);
int audio_play(int track_index, const char *path);
int audio_pause(void);
int audio_resume(void);
int audio_stop(void);
int audio_set_volume(int volume_percent);
void audio_get_snapshot(audio_snapshot_t *out);

#endif
