#include <dirent.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

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
    return strcasecmp(dot, ".mp3") == 0 || strcasecmp(dot, ".wav") == 0 ||
           strcasecmp(dot, ".ogg") == 0 || strcasecmp(dot, ".flac") == 0;
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
    if (!strcmp(command, "PLAYLIST")) playlist(client);
    else if (!strcmp(command, "STATUS")) send_all(client, "ERR audio-backend-not-implemented\n");
    else if (!strcmp(command, "NEXT") || !strcmp(command, "PREV") ||
             !strcmp(command, "PAUSE") || !strcmp(command, "RESUME") ||
             !strcmp(command, "STOP")) send_all(client, "ERR audio-backend-not-implemented\n");
    else send_all(client, "ERR commands: PLAYLIST, STATUS, NEXT, PREV, PAUSE, RESUME, STOP\n");
}

int main(void) {
    mkdir(MUSIC_ROOT, 0755);
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
