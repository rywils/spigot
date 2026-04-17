#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "http.h"

int is_number(const char *str) {
    for (int i = 0; str[i]; i++) {
        if (str[i] < '0' || str[i] > '9') return 0;
    }
    return str[0] != '\0';
}

int parse_port(const char *str, int *port_out) {
    if (!is_number(str)) return 0;

    int port = atoi(str);
    if (port < 1024 || port > 65535) return -1;

    *port_out = port;
    return 1;
}

void send_error_response(int client_fd, int status_code, const char *status_text,
                         const char *content_type, const char *body) {
    char header[256];
    snprintf(header, sizeof(header),
             "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
             status_code, status_text, content_type, strlen(body));
    send(client_fd, header, strlen(header), 0);
    send(client_fd, body, strlen(body), 0);
}

int url_decode(const char *src, char *dst, int dst_size) {
    int i = 0, j = 0;
    while (src[i] && j < dst_size - 1) {
        if (src[i] == '%' && src[i+1] && src[i+2]) {
            int c1 = src[i+1];
            int c2 = src[i+2];
            int val = 0;
            if (c1 >= '0' && c1 <= '9') val += (c1 - '0') << 4;
            else if (c1 >= 'A' && c1 <= 'F') val += (c1 - 'A' + 10) << 4;
            else if (c1 >= 'a' && c1 <= 'f') val += (c1 - 'a' + 10) << 4;
            else { dst[j++] = src[i++]; continue; }
            if (c2 >= '0' && c2 <= '9') val += (c2 - '0');
            else if (c2 >= 'A' && c2 <= 'F') val += (c2 - 'A' + 10);
            else if (c2 >= 'a' && c2 <= 'f') val += (c2 - 'a' + 10);
            else { dst[j++] = src[i++]; continue; }
            dst[j++] = (char)val;
            i += 3;
        } else if (src[i] == '+') {
            dst[j++] = ' ';
            i++;
        } else {
            dst[j++] = src[i++];
        }
    }
    dst[j] = '\0';
    return j;
}
