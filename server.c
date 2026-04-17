#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "server.h"
#include "http.h"
#include "file.h"

#define BUFFER_SIZE 4096

struct client_info {
    int client_fd;
    char base_dir[256];
    int enable_reload;
};

void* handle_client_thread(void* arg) {
    struct client_info* info = (struct client_info*)arg;
    int client_fd = info->client_fd;
    const char* base_dir = info->base_dir;

    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));


    ssize_t bytes_read = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_read <= 0) {
        close(client_fd);
        free(info);
        return NULL;
    }

    char method[16];
    char raw_path[256];
    if (sscanf(buffer, "%15s %255s", method, raw_path) != 2) {
        close(client_fd);
        free(info);
        return NULL;
    }

    char path[256];
    url_decode(raw_path, path, sizeof(path));


    if (path[0] != '/') {
        send_error_response(client_fd, 403, "Forbidden", "text/plain", "Forbidden\n");
        printf("[%s] %s → 403\n", method, path);
        close(client_fd);
        free(info);
        return NULL;
    }

    const char *p = path;
    while (*p) {
        if (p[0] == '/' && p[1] == '.') {
            if (p[2] == '.' && (p[3] == '/' || p[3] == '\0')) {
                send_error_response(client_fd, 403, "Forbidden", "text/plain", "Forbidden\n");
                printf("[%s] %s → 403\n", method, path);
                close(client_fd);
                free(info);
                return NULL;
            }
            if (p[2] == '/' || p[2] == '\0') {
                send_error_response(client_fd, 403, "Forbidden", "text/plain", "Forbidden\n");
                printf("[%s] %s → 403\n", method, path);
                close(client_fd);
                free(info);
                return NULL;
            }
        }
        if (p[0] == '/' && p[1] == '/') {
            send_error_response(client_fd, 403, "Forbidden", "text/plain", "Forbidden\n");
            printf("[%s] %s → 403\n", method, path);
            close(client_fd);
            free(info);
            return NULL;
        }
        p++;
    }

    if (strcmp(path, "/__reload_check") == 0) {
        if (!info->enable_reload) {
            send_error_response(client_fd, 404, "Not Found", "text/plain", "Not Found\n");
            printf("[%s] %s → 404 (reload disabled)\n", method, path);
            close(client_fd);
            free(info);
            return NULL;
        }
        int changed = check_reload(base_dir);
        const char *body = changed ? "1" : "0";
        char header[256];
        snprintf(header, sizeof(header),
                 "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                 strlen(body));
        send(client_fd, header, strlen(header), 0);
        send(client_fd, body, strlen(body), 0);
        printf("[%s] %s → 200 (reload: %s)\n", method, path, body);
        close(client_fd);
        free(info);
        return NULL;
    }

    const char *file_path = path + 1;
    if (file_path[0] == '\0') {
        file_path = "index.html";
    }

    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s", base_dir, file_path);

    int status = send_response(client_fd, full_path, path);
    printf("[%s] %s → %d\n", method, path, status);
    close(client_fd);

    free(info);
    return NULL;
}

void server_start(const char *base_dir, int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(1);
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(1);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        exit(1);
    }

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }
        struct client_info* info = malloc(sizeof(struct client_info));
        if (!info) {
            close(client_fd);
            continue;
        }
        info->client_fd = client_fd;
        strncpy(info->base_dir, base_dir, sizeof(info->base_dir) - 1);
        info->base_dir[sizeof(info->base_dir) - 1] = '\0';
        info->enable_reload = reload_enabled;

        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_client_thread, info) != 0) {
            free(info);
            close(client_fd);
            continue;
        }
        pthread_detach(thread);
    }

    close(server_fd);
}
