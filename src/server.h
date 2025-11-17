#ifndef SERVER_H
#define SERVER_H

#include <stddef.h>
#include <stdint.h>

struct server_config {
    int port;
    int max_events;
    int listen_backlog;
    int max_clients;
    size_t client_buffer_size;
};

struct server_stats {
    uint64_t total_tcp_clients;
    uint64_t current_tcp_clients;
    uint64_t total_udp_messages;
};

int server_run(const struct server_config *cfg);

int server_process_line(const char *line,
                        size_t len,
                        const struct server_stats *stats,
                        int *shutdown_requested,
                        char *out,
                        size_t out_cap);

#endif
