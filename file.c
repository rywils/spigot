#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/socket.h>

#include "file.h"
#include "http.h"

time_t last_modified_time = 0;
time_t last_scan_time = 0;
int reload_enabled = 0;
static pthread_mutex_t reload_mutex = PTHREAD_MUTEX_INITIALIZER;

const char *get_content_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    ext++;
    if (strcasecmp(ext, "html") == 0 || strcasecmp(ext, "htm") == 0) return "text/html";
    if (strcasecmp(ext, "css") == 0) return "text/css";
    if (strcasecmp(ext, "js") == 0) return "application/javascript";
    if (strcasecmp(ext, "png") == 0) return "image/png";
    if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0) return "image/jpeg";
    return "application/octet-stream";
}

int is_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode);
}

time_t get_latest_modification(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    time_t max_mtime = st.st_mtime;

    DIR *dir = opendir(path);
    if (!dir) return max_mtime;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.' && (entry->d_name[1] == '\0' ||
            (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) continue;

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        if (stat(full_path, &st) == 0) {
            if (st.st_mtime > max_mtime) {
                max_mtime = st.st_mtime;
            }
            if (S_ISDIR(st.st_mode)) {
                time_t sub_mtime = get_latest_modification(full_path);
                if (sub_mtime > max_mtime) {
                    max_mtime = sub_mtime;
                }
            }
        }
    }
    closedir(dir);
    return max_mtime;
}

int check_reload(const char *base_dir) {
    pthread_mutex_lock(&reload_mutex);

    time_t now = time(NULL);
    int changed = 0;

    if (now - last_scan_time >= 1) {
        time_t current_mtime = get_latest_modification(base_dir);
        last_scan_time = now;

        if (last_modified_time == 0) {
            last_modified_time = current_mtime;
        } else if (current_mtime > last_modified_time) {
            last_modified_time = current_mtime;
            changed = 1;
        }
    }

    pthread_mutex_unlock(&reload_mutex);
    return changed;
}

void send_directory_listing(int client_fd, const char *dir_path, const char *url_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        send_error_response(client_fd, 404, "Not Found", "text/html",
                              "<h1>404 Not Found</h1><p>The requested file was not found.</p>");
        return;
    }


    char html[65536];
    int pos = 0;
    pos += snprintf(html + pos, sizeof(html) - pos,
                      "<html><head><title>Index of %s</title></head><body>", url_path);
    pos += snprintf(html + pos, sizeof(html) - pos, "<h1>Index of %s</h1><ul>", url_path);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        pos += snprintf(html + pos, sizeof(html) - pos,
                        "<li><a href=\"%s\">%s</a></li>", entry->d_name, entry->d_name);
    }
    closedir(dir);

    pos += snprintf(html + pos, sizeof(html) - pos, "</ul></body></html>");


    char header[256];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
             pos);
    send(client_fd, header, strlen(header), 0);
    send(client_fd, html, pos, 0);
}

int send_response(int client_fd, const char *file_path, const char *url_path) {
    if (is_directory(file_path)) {
        char index_path[512];
        snprintf(index_path, sizeof(index_path), "%s/index.html", file_path);
        FILE *fp = fopen(index_path, "rb");
        if (fp) {
            fclose(fp);
            return send_response(client_fd, index_path, url_path);
        }
        send_directory_listing(client_fd, file_path, url_path);
        return 200;
    }

    FILE *fp = fopen(file_path, "rb");
    if (!fp) {
        send_error_response(client_fd, 404, "Not Found", "text/html",
                            "<h1>404 Not Found</h1><p>The requested file was not found.</p>");
        return 404;
    }


    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    const char *content_type = get_content_type(file_path);

    if (reload_enabled && strcmp(content_type, "text/html") == 0) {
        char *file_content = malloc(file_size + 1);
        if (file_content) {
            if (fread(file_content, 1, file_size, fp) == (size_t)file_size) {
                file_content[file_size] = '\0';

                const char *reload_script = "<script>setInterval(async()=>{const r=await fetch('/__reload_check');const t=await r.text();if(t==='1')location.reload()},1000)</script>";

                char *body_end = strstr(file_content, "</body>");
                char *modified_content;
                long modified_size;

                if (body_end) {
                    modified_size = file_size + strlen(reload_script);
                    modified_content = malloc(modified_size);
                    if (modified_content) {
                        int prefix_len = body_end - file_content;
                        memcpy(modified_content, file_content, prefix_len);
                        memcpy(modified_content + prefix_len, reload_script, strlen(reload_script));
                        memcpy(modified_content + prefix_len + strlen(reload_script), body_end, file_size - prefix_len);
                    }
                } else {
                    modified_size = file_size + strlen(reload_script);
                    modified_content = malloc(modified_size);
                    if (modified_content) {
                        memcpy(modified_content, file_content, file_size);
                        memcpy(modified_content + file_size, reload_script, strlen(reload_script));
                    }
                }

                if (modified_content) {
                    char header[256];
                    snprintf(header, sizeof(header),
                             "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n",
                             modified_size);
                    send(client_fd, header, strlen(header), 0);
                    send(client_fd, modified_content, modified_size, 0);
                    free(modified_content);
                }
                free(file_content);
            } else {
                free(file_content);
            }
        }
        fclose(fp);
        return 200;
    }

    char header[256];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n",
             content_type, file_size);
    send(client_fd, header, strlen(header), 0);

    char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        send(client_fd, buffer, n, 0);
    }

    fclose(fp);
    return 200;
}
