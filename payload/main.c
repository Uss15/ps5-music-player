#include <dirent.h>
#include <errno.h>
#include <netinet/in.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include "audio.h"

#ifndef MUSIC_ROOT
#define MUSIC_ROOT "/data/music"
#endif
#ifndef USB0_ROOT
#define USB0_ROOT "/mnt/usb0"
#endif
#ifndef USB1_ROOT
#define USB1_ROOT "/mnt/usb1"
#endif
#ifndef PORT
#define PORT 9023
#endif
#define MAX_TRACKS 2048
#define PATH_CAP 1024

static char tracks[MAX_TRACKS][PATH_CAP];
static size_t track_count;

static int send_all(int fd, const char *text) {
    size_t left = strlen(text);
    while (left) {
        ssize_t sent = send(fd, text, left, 0);
        if (sent < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        text += sent;
        left -= (size_t)sent;
    }
    return 0;
}

static int supported(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return 0;
    return strcasecmp(dot, ".mp3") == 0 || strcasecmp(dot, ".wav") == 0;
}

static void scan_dir(const char *root, const char *relative, unsigned depth) {
    if (depth > 32 || track_count >= MAX_TRACKS) return;
    char directory[PATH_CAP];
    int n = relative[0] ? snprintf(directory, sizeof(directory), "%s/%s", root, relative)
                        : snprintf(directory, sizeof(directory), "%s", root);
    if (n < 0 || (size_t)n >= sizeof(directory)) return;

    DIR *dir = opendir(directory);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && track_count < MAX_TRACKS) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char child_relative[PATH_CAP];
        n = relative[0] ? snprintf(child_relative, sizeof(child_relative), "%s/%s", relative, entry->d_name)
                        : snprintf(child_relative, sizeof(child_relative), "%s", entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(child_relative)) continue;
        char full[PATH_CAP];
        n = snprintf(full, sizeof(full), "%s/%s", root, child_relative);
        if (n < 0 || (size_t)n >= sizeof(full)) continue;

        struct stat st;
        if (lstat(full, &st) != 0 || S_ISLNK(st.st_mode)) continue;
        if (S_ISDIR(st.st_mode)) {
            scan_dir(root, child_relative, depth + 1);
        } else if (S_ISREG(st.st_mode) && supported(entry->d_name) && track_count < MAX_TRACKS) {
            snprintf(tracks[track_count], sizeof(tracks[track_count]), "%s", full);
            track_count++;
        }
    }
    closedir(dir);
}

static int compare_tracks(const void *a, const void *b) {
    return strcasecmp((const char *)a, (const char *)b);
}

static void rebuild_playlist(void) {
    track_count = 0;
    scan_dir(MUSIC_ROOT, "", 0);
    scan_dir(USB0_ROOT, "", 0);
    scan_dir(USB1_ROOT, "", 0);
    qsort(tracks, track_count, sizeof(tracks[0]), compare_tracks);
}

static void playlist(int client) {
    rebuild_playlist();
    if (send_all(client, "OK\n") != 0) return;
    for (size_t i = 0; i < track_count; i++) {
        char line[PATH_CAP + 32];
        int n = snprintf(line, sizeof(line), "%zu\t%s\n", i + 1, tracks[i]);
        if (n < 0 || (size_t)n >= sizeof(line) || send_all(client, line) != 0) return;
    }
    send_all(client, ".\n");
}

static const char *state_name(audio_state_t state) {
    switch (state) {
        case AUDIO_STATE_STOPPED: return "STOPPED";
        case AUDIO_STATE_LOADING: return "LOADING";
        case AUDIO_STATE_PLAYING: return "PLAYING";
        case AUDIO_STATE_PAUSED: return "PAUSED";
        case AUDIO_STATE_ERROR: return "ERROR";
        default: return "UNAVAILABLE";
    }
}

static void send_status(int client) {
    audio_snapshot_t snapshot;
    audio_get_snapshot(&snapshot);
    char line[256];
    snprintf(line, sizeof(line), "OK\n%s\t%d\t%llu\t%llu\t%d\t%d\t%s\n",
             state_name(snapshot.state), snapshot.track_index,
             (unsigned long long)snapshot.position_ms,
             (unsigned long long)snapshot.duration_ms,
             snapshot.volume, audio_ready(), snapshot.error);
    send_all(client, line);
}

static int play_track(int client, long requested) {
    rebuild_playlist();
    if (requested < 1 || (size_t)requested > track_count) {
        send_all(client, "ERR invalid-track-index\n");
        return -1;
    }
    if (audio_play((int)requested, tracks[requested - 1]) != 0) {
        send_all(client, "ERR audio-output-unavailable\n");
        return -1;
    }
    send_all(client, "OK\nQUEUED\n");
    return 0;
}

static int parse_number(const char *text, long *value) {
    if (!text || !*text) return -1;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        if (!isdigit(*p)) return -1;
    errno = 0;
    char *end = NULL;
    long parsed = strtol(text, &end, 10);
    if (errno || !end || *end) return -1;
    *value = parsed;
    return 0;
}

static void handle_client(int client) {
    char command[128] = {0};
    size_t used = 0;
    while (used < sizeof(command) - 1) {
        char c;
        ssize_t n = recv(client, &c, 1, 0);
        if (n <= 0) return;
        if (c == '\n' || c == '\r') break;
        command[used++] = c;
    }
    if (!strcmp(command, "PLAYLIST")) { playlist(client); return; }
    if (!strcmp(command, "STATUS")) { send_status(client); return; }
    if (!strncmp(command, "PLAY ", 5)) {
        long index;
        if (parse_number(command + 5, &index) != 0) send_all(client, "ERR invalid-track-index\n");
        else play_track(client, index);
        return;
    }
    if (!strncmp(command, "VOLUME ", 7)) {
        long volume;
        if (parse_number(command + 7, &volume) != 0 || volume > 100 || audio_set_volume((int)volume) != 0)
            send_all(client, "ERR volume-must-be-0-through-100\n");
        else send_all(client, "OK\nVOLUME_SET\n");
        return;
    }
    if (!strcmp(command, "NEXT") || !strcmp(command, "PREV")) {
        rebuild_playlist();
        if (!track_count) { send_all(client, "ERR no-tracks\n"); return; }
        audio_snapshot_t snapshot;
        audio_get_snapshot(&snapshot);
        long current = snapshot.track_index;
        long next = !strcmp(command, "NEXT") ? current + 1 : current - 1;
        if (current < 1) next = !strcmp(command, "NEXT") ? 1 : (long)track_count;
        if (next > (long)track_count) next = 1;
        if (next < 1) next = (long)track_count;
        play_track(client, next);
        return;
    }
    if (!strcmp(command, "PAUSE")) {
        if (audio_pause() == 0) send_all(client, "OK\nPAUSED\n"); else send_all(client, "ERR cannot-pause\n");
        return;
    }
    if (!strcmp(command, "RESUME")) {
        if (audio_resume() == 0) send_all(client, "OK\nRESUMED\n"); else send_all(client, "ERR cannot-resume\n");
        return;
    }
    if (!strcmp(command, "STOP")) {
        if (audio_stop() == 0) send_all(client, "OK\nSTOPPED\n"); else send_all(client, "ERR audio-output-unavailable\n");
        return;
    }
    send_all(client, "ERR commands: PLAYLIST, PLAY n, STATUS, NEXT, PREV, PAUSE, RESUME, STOP, VOLUME n\n");
}

int main(void) {
    mkdir(MUSIC_ROOT, 0755);
    if (audio_init() == 0) printf("PS5 BGM audio output initialized.\n");
    else fprintf(stderr, "PS5 audio output unavailable; playlist service will remain available.\n");
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) { perror("socket"); return 1; }
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) < 0 || listen(server, 4) < 0) {
        perror("bind/listen"); close(server); return 1;
    }
    printf("PS5 Music Player library service listening on %d\n", PORT);
    for (;;) {
        int client = accept(server, NULL, NULL);
        if (client < 0) { if (errno == EINTR) continue; perror("accept"); continue; }
        handle_client(client);
        close(client);
    }
}
