#include <stdio.h>
#include <string.h>

#include "http.h"
#include "file.h"
#include "server.h"

#define DEFAULT_PORT 8080
#define DEFAULT_DIR "."

int main(int argc, char *argv[]) {
    const char *base_dir = DEFAULT_DIR;
    int port = DEFAULT_PORT;


    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--reload") == 0) {
            reload_enabled = 1;
        } else if (is_number(argv[i])) {
            int result = parse_port(argv[i], &port);
            if (result == -1) {
                fprintf(stderr, "Error: invalid port '%s' (must be 1024-65535)\n", argv[i]);
                return 1;
            }
        } else {
            base_dir = argv[i];
        }
    }

    printf("Serving directory: %s\n", base_dir);
    printf("URL: http://localhost:%d\n", port);

    server_start(base_dir, port);
    return 0;
}
