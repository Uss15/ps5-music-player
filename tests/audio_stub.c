#include <stdint.h>

int32_t sceAudioOutInit(void) { return 0; }

int32_t sceAudioOutOpen(int32_t user_id, int32_t port_type, int32_t index,
                        uint32_t length, uint32_t sample_rate, uint32_t param_type) {
    (void)user_id; (void)port_type; (void)index; (void)length; (void)sample_rate; (void)param_type;
    return 17;
}
int32_t sceAudioOutOutput(int32_t handle, const void *samples) {
    return handle == 17 && samples ? 0 : -1;
}
int32_t sceAudioOutClose(int32_t handle) { return handle == 17 ? 0 : -1; }
