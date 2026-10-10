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

#define ROOT "/data/music"
#define PORT 9023

static void reply_text(int client, const char *text) {
    send(client, text, strlen(text), 0);
}

static int supported(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    return strcasecmp(dot, ".mp3") == 0 || strcasecmp(dot, ".wav") == 0 || strcasecmp(dot, ".ogg") == 0 || strcasecmp(dot, ".flac") == 0;
}

static void playlist(int client) {
    DIR *dir = opendir(ROOT);
    if (!dir) { reply_text(client, "ERR cannot-open-music-directory\n"); return; }
    reply_text(client, "OK\n");
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!supported(entry->d_name)) continue;
        char line[512];
        snprintf(line, sizeof(line), "%s\n", entry->d_name);
        reply_text(client, line);
    }
    reply_text(client, ".\n");
    closedir(dir);
}

int main(void) {
    mkdir(ROOT, 0755);
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) { perror("socket"); return 1; }
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) < 0 || listen(server, 4) < 0) {
        perror("bind/listen"); return 1;
    }
    printf("PS5 Music Player playlist service listening on %d\n", PORT);
    for (;;) {
        int client = accept(server, NULL, NULL);
        if (client < 0) continue;
        char command[64] = {0};
        ssize_t length = recv(client, command, sizeof(command) - 1, 0);
        if (length > 0 && strncmp(command, "PLAYLIST", 8) == 0) playlist(client);
        else if (length > 0 && strncmp(command, "NEXT", 4) == 0) reply_text(client, "OK audio-backend-pending\n");
        else reply_text(client, "ERR commands: PLAYLIST, NEXT\n");
        close(client);
    }
}
