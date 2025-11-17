#include "server.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    int port = 12345;
    if (argc >= 2) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "invalid port: %s\n", argv[1]);
            return 1;
        }
    }
    struct server_config cfg;
    cfg.port = port;
    cfg.max_events = 64;
    cfg.listen_backlog = 128;
    cfg.max_clients = 1024;
    cfg.client_buffer_size = 4096;
    int rc = server_run(&cfg);
    return rc == 0 ? 0 : 1;
}
