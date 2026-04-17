#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

int is_number(const char *str);
int parse_port(const char *str, int *port_out);
void send_error_response(int client_fd, int status_code, const char *status_text,
                         const char *content_type, const char *body);
int url_decode(const char *src, char *dst, int dst_size);

#endif
