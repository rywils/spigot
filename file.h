#ifndef FILE_H
#define FILE_H

#include <time.h>

extern time_t last_modified_time;
extern time_t last_scan_time;
extern int reload_enabled;

const char *get_content_type(const char *path);
int is_directory(const char *path);
time_t get_latest_modification(const char *path);
int check_reload(const char *base_dir);
void send_directory_listing(int client_fd, const char *dir_path, const char *url_path);
int send_response(int client_fd, const char *file_path, const char *url_path);

#endif
