#include <limits.h>
#include <stdarg.h>
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

/* Lexical checks in server.c only catch ".." in the request path - a
 * symlink physically sitting inside base_dir (e.g. `ln -s /etc leak`)
 * would still be followed by stat()/fopen() straight out of the served
 * tree. realpath() both sides and confirm full_path resolves to base_dir
 * itself or somewhere inside it.
 *
 * A full_path that doesn't exist yet is treated as safe (returns 1) -
 * realpath() can't resolve something that isn't there, and that's an
 * ordinary lookup miss for the caller's normal 404 path, not a
 * containment violation. Only an existing path that resolves outside
 * base_dir is rejected.
 *
 * Note: there's a TOCTOU window between this check and the fopen() that
 * follows it - a symlink could theoretically be swapped in between. Not
 * closing that here (would need per-component openat()+O_NOFOLLOW
 * resolution) since this is a single-request, no-auth local dev server,
 * not a multi-tenant host; the realistic threat here is a static symlink
 * sitting in the tree, which this does catch. */
static int path_is_within_base(const char *full_path, const char *base_dir) {
    char resolved_base[PATH_MAX];
    char resolved_full[PATH_MAX];

    if (!realpath(base_dir, resolved_base)) return 0;
    if (!realpath(full_path, resolved_full)) return 1;

    size_t base_len = strlen(resolved_base);
    if (strncmp(resolved_full, resolved_base, base_len) != 0) return 0;

    /* Require an exact match or a '/' boundary, so base_dir "/srv/www"
     * doesn't wrongly match a sibling like "/srv/wwwevil". */
    return resolved_full[base_len] == '\0' || resolved_full[base_len] == '/';
}

/* Appends to buf like snprintf, but tracks *pos so remaining space never
 * goes negative/wraps once the buffer fills - stops appending instead of
 * overflowing. */
static void append_bounded(char *buf, size_t buf_size, int *pos, const char *fmt, ...) {
    if (*pos < 0 || (size_t)*pos >= buf_size) return;

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buf + *pos, buf_size - (size_t)*pos, fmt, args);
    va_end(args);

    if (written < 0) return;
    *pos += written;
    if ((size_t)*pos > buf_size) *pos = (int)buf_size;
}

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

        /* lstat, not stat: a symlinked directory must not be followed into
         * recursion, or a symlink cycle (e.g. `ln -s . self`) inside the
         * served directory causes unbounded recursion and a stack overflow. */
        if (lstat(full_path, &st) == 0) {
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
    append_bounded(html, sizeof(html), &pos,
                   "<html><head><title>Index of %s</title></head><body>", url_path);
    append_bounded(html, sizeof(html), &pos, "<h1>Index of %s</h1><ul>", url_path);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        append_bounded(html, sizeof(html), &pos,
                       "<li><a href=\"%s\">%s</a></li>", entry->d_name, entry->d_name);
    }
    closedir(dir);

    append_bounded(html, sizeof(html), &pos, "</ul></body></html>");

    char header[256];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
             pos);
    send(client_fd, header, strlen(header), 0);
    send(client_fd, html, pos, 0);
}

int send_response(int client_fd, const char *file_path, const char *url_path, const char *base_dir) {
    if (!path_is_within_base(file_path, base_dir)) {
        send_error_response(client_fd, 403, "Forbidden", "text/plain", "Forbidden\n");
        return 403;
    }

    if (is_directory(file_path)) {
        char index_path[512];
        snprintf(index_path, sizeof(index_path), "%s/index.html", file_path);
        FILE *fp = fopen(index_path, "rb");
        if (fp) {
            fclose(fp);
            return send_response(client_fd, index_path, url_path, base_dir);
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
        char *modified_content = NULL;
        long modified_size = 0;

        if (file_content && fread(file_content, 1, file_size, fp) == (size_t)file_size) {
            file_content[file_size] = '\0';

            const char *reload_script = "<script>setInterval(async()=>{const r=await fetch('/__reload_check');const t=await r.text();if(t==='1')location.reload()},1000)</script>";
            char *body_end = strstr(file_content, "</body>");
            modified_size = file_size + (long)strlen(reload_script);
            modified_content = malloc(modified_size);

            if (modified_content) {
                if (body_end) {
                    long prefix_len = body_end - file_content;
                    memcpy(modified_content, file_content, prefix_len);
                    memcpy(modified_content + prefix_len, reload_script, strlen(reload_script));
                    memcpy(modified_content + prefix_len + strlen(reload_script), body_end, file_size - prefix_len);
                } else {
                    memcpy(modified_content, file_content, file_size);
                    memcpy(modified_content + file_size, reload_script, strlen(reload_script));
                }
            }
        }
        free(file_content);

        if (modified_content) {
            char header[256];
            snprintf(header, sizeof(header),
                     "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n",
                     modified_size);
            send(client_fd, header, strlen(header), 0);
            send(client_fd, modified_content, modified_size, 0);
            free(modified_content);
            fclose(fp);
            return 200;
        }

        /* Injection failed (OOM or short read) - fall back to serving the
         * file unmodified instead of silently dropping the response. */
        fseek(fp, 0, SEEK_SET);
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
