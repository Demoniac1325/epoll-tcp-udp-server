#define _GNU_SOURCE
#include "server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

struct client {
    int fd;
    char *buf;
    size_t len;
    size_t cap;
    int alive;
};

struct server_state {
    int epfd;
    int tcp_listen_fd;
    int udp_fd;
    struct client *clients;
    size_t clients_count;
    size_t clients_cap;
    struct server_stats stats;
    int shutdown_requested;
    size_t client_buf_size;
    int max_clients;
};

static void log_ts(char *buf, size_t cap) {
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(buf, cap, "%Y-%m-%d %H:%M:%S", &tm_now);
}

static void log_info(const char *fmt, ...) {
    char ts[32];
    log_ts(ts, sizeof(ts));
    fprintf(stdout, "[%s] [INFO] ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
    fflush(stdout);
}

static void log_error(const char *fmt, ...) {
    char ts[32];
    log_ts(ts, sizeof(ts));
    fprintf(stderr, "[%s] [ERROR] ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

static int make_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) return -1;
    return 0;
}

static int add_fd_epoll(int epfd, int fd, uint32_t events) {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == -1) return -1;
    return 0;
}

static int setup_tcp_listener(int port, int backlog) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("socket tcp");
        return -1;
    }
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        close(fd);
        return -1;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind tcp");
        close(fd);
        return -1;
    }
    if (listen(fd, backlog) == -1) {
        perror("listen");
        close(fd);
        return -1;
    }
    if (make_nonblocking(fd) == -1) {
        perror("nonblock tcp listen");
        close(fd);
        return -1;
    }
    return fd;
}

static int setup_udp_socket(int port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) {
        perror("socket udp");
        return -1;
    }
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        close(fd);
        return -1;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind udp");
        close(fd);
        return -1;
    }
    if (make_nonblocking(fd) == -1) {
        perror("nonblock udp");
        close(fd);
        return -1;
    }
    return fd;
}

static struct client *find_client(struct server_state *st, int fd) {
    for (size_t i = 0; i < st->clients_count; i++) {
        if (st->clients[i].fd == fd && st->clients[i].alive) return &st->clients[i];
    }
    return NULL;
}

static int add_client(struct server_state *st, int fd) {
    if (st->max_clients > 0 && (int)st->stats.current_tcp_clients >= st->max_clients) {
        log_error("max clients reached, closing fd=%d", fd);
        close(fd);
        return -1;
    }
    if (st->clients_count == st->clients_cap) {
        size_t new_cap = st->clients_cap ? st->clients_cap * 2 : 16;
        struct client *nc = realloc(st->clients, new_cap * sizeof(struct client));
        if (!nc) {
            close(fd);
            return -1;
        }
        st->clients = nc;
        st->clients_cap = new_cap;
    }
    struct client c;
    c.fd = fd;
    c.buf = malloc(st->client_buf_size);
    if (!c.buf) {
        close(fd);
        return -1;
    }
    c.len = 0;
    c.cap = st->client_buf_size;
    c.alive = 1;
    st->clients[st->clients_count++] = c;
    st->stats.total_tcp_clients++;
    st->stats.current_tcp_clients++;
    return 0;
}

static void close_client(struct server_state *st, struct client *c) {
    if (!c->alive) return;
    epoll_ctl(st->epfd, EPOLL_CTL_DEL, c->fd, NULL);
    close(c->fd);
    c->alive = 0;
    if (c->buf) {
        free(c->buf);
        c->buf = NULL;
    }
    if (st->stats.current_tcp_clients > 0) st->stats.current_tcp_clients--;
}

static void trim_line(char *line, size_t *len) {
    while (*len > 0 && (line[*len - 1] == '\n' || line[*len - 1] == '\r' || line[*len - 1] == ' ' || line[*len - 1] == '\t')) {
        line[*len - 1] = '\0';
        (*len)--;
    }
    size_t start = 0;
    while (start < *len && (line[start] == ' ' || line[start] == '\t')) start++;
    if (start > 0 && start < *len) {
        memmove(line, line + start, *len - start);
        *len -= start;
        line[*len] = '\0';
    } else if (start >= *len) {
        *len = 0;
        line[0] = '\0';
    }
}

int server_process_line(const char *line,
                        size_t len,
                        const struct server_stats *stats,
                        int *shutdown_requested,
                        char *out,
                        size_t out_cap) {
    if (!line || !stats || !shutdown_requested || !out || out_cap == 0) return -1;
    if (len == 0) return 0;
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' || line[len - 1] == ' ' || line[len - 1] == '\t')) len--;
    size_t start = 0;
    while (start < len && (line[start] == ' ' || line[start] == '\t')) start++;
    if (start >= len) return 0;
    const char *p = line + start;
    size_t plen = len - start;
    if (p[0] != '/') {
        if (plen + 2 > out_cap) return -1;
        memcpy(out, p, plen);
        out[plen] = '\n';
        out[plen + 1] = '\0';
        return (int)(plen + 1);
    }
    char cmd[64];
    size_t clen = plen < sizeof(cmd) - 1 ? plen : sizeof(cmd) - 1;
    memcpy(cmd, p, clen);
    cmd[clen] = '\0';
    char *sp = strchr(cmd, ' ');
    if (sp) *sp = '\0';
    if (strcmp(cmd, "/time") == 0) {
        time_t now = time(NULL);
        struct tm tm_now;
        if (!localtime_r(&now, &tm_now)) return -1;
        if (out_cap < 32) return -1;
        size_t n = strftime(out, out_cap, "%Y-%m-%d %H:%M:%S", &tm_now);
        if (n == 0 || n + 2 > out_cap) return -1;
        out[n] = '\n';
        out[n + 1] = '\0';
        return (int)(n + 1);
    } else if (strcmp(cmd, "/stats") == 0) {
        int n = snprintf(out,
                         out_cap,
                         "total_tcp_clients=%" PRIu64 " current_tcp_clients=%" PRIu64 " total_udp_messages=%" PRIu64 "\n",
                         stats->total_tcp_clients,
                         stats->current_tcp_clients,
                         stats->total_udp_messages);
        if (n < 0 || (size_t)n >= out_cap) return -1;
        return n;
    } else if (strcmp(cmd, "/help") == 0) {
        const char *help =
            "Available commands:\n"
            "/time\n"
            "/stats\n"
            "/shutdown\n"
            "/help\n";
        size_t hlen = strlen(help);
        if (hlen + 1 > out_cap) return -1;
        memcpy(out, help, hlen + 1);
        return (int)hlen;
    } else if (strcmp(cmd, "/shutdown") == 0) {
        *shutdown_requested = 1;
        const char *m = "shutting down\n";
        size_t mlen = strlen(m);
        if (mlen + 1 > out_cap) return -1;
        memcpy(out, m, mlen + 1);
        return (int)mlen;
    } else {
        const char *u = "unknown command\n";
        size_t ulen = strlen(u);
        if (ulen + 1 > out_cap) return -1;
        memcpy(out, u, ulen + 1);
        return (int)ulen;
    }
}

static void handle_tcp_accept(struct server_state *st) {
    for (;;) {
        struct sockaddr_in addr;
        socklen_t alen = sizeof(addr);
        int cfd = accept(st->tcp_listen_fd, (struct sockaddr *)&addr, &alen);
        if (cfd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            perror("accept");
            break;
        }
        if (make_nonblocking(cfd) == -1) {
            perror("nonblock client");
            close(cfd);
            continue;
        }
        if (add_fd_epoll(st->epfd, cfd, EPOLLIN) == -1) {
            perror("epoll add client");
            close(cfd);
            continue;
        }
        if (add_client(st, cfd) == -1) {
            log_error("failed to add client fd=%d", cfd);
            continue;
        }
        char ip[64];
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
        log_info("tcp client fd=%d from %s:%d", cfd, ip, ntohs(addr.sin_port));
    }
}

static void handle_tcp_client(struct server_state *st, int fd) {
    struct client *c = find_client(st, fd);
    if (!c) {
        epoll_ctl(st->epfd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
        return;
    }
    for (;;) {
        char tmp[1024];
        ssize_t n = recv(fd, tmp, sizeof(tmp), 0);
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            perror("recv");
            close_client(st, c);
            return;
        }
        if (n == 0) {
            close_client(st, c);
            return;
        }
        if (c->len + (size_t)n > c->cap) {
            size_t new_cap = c->cap * 2;
            while (new_cap < c->len + (size_t)n) new_cap *= 2;
            char *nb = realloc(c->buf, new_cap);
            if (!nb) {
                close_client(st, c);
                return;
            }
            c->buf = nb;
            c->cap = new_cap;
        }
        memcpy(c->buf + c->len, tmp, (size_t)n);
        c->len += (size_t)n;
        size_t pos = 0;
        while (pos < c->len) {
            char *nl = memchr(c->buf + pos, '\n', c->len - pos);
            if (!nl) break;
            size_t line_len = (size_t)(nl - (c->buf + pos) + 1);
            char line[2048];
            size_t copy_len = line_len < sizeof(line) - 1 ? line_len : sizeof(line) - 1;
            memcpy(line, c->buf + pos, copy_len);
            line[copy_len] = '\0';
            size_t logical_len = copy_len;
            trim_line(line, &logical_len);
            char out[4096];
            int shutdown_flag = st->shutdown_requested;
            int out_len = server_process_line(line, logical_len, &st->stats, &shutdown_flag, out, sizeof(out));
            if (!st->shutdown_requested && shutdown_flag) {
                st->shutdown_requested = 1;
                log_info("shutdown requested by tcp fd=%d", fd);
            }
            if (out_len > 0) {
                size_t sent = 0;
                while (sent < (size_t)out_len) {
                    ssize_t s = send(fd, out + sent, (size_t)out_len - sent, 0);
                    if (s == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        perror("send");
                        close_client(st, c);
                        return;
                    }
                    if (s == 0) break;
                    sent += (size_t)s;
                }
            }
            pos += line_len;
        }
        if (pos > 0) {
            if (pos < c->len) memmove(c->buf, c->buf + pos, c->len - pos);
            c->len -= pos;
        }
    }
}

static void handle_udp(struct server_state *st) {
    for (;;) {
        char buf[2048];
        struct sockaddr_in addr;
        socklen_t alen = sizeof(addr);
        ssize_t n = recvfrom(st->udp_fd, buf, sizeof(buf), 0, (struct sockaddr *)&addr, &alen);
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            perror("recvfrom");
            break;
        }
        if (n == 0) break;
        st->stats.total_udp_messages++;
        char line[2048];
        size_t copy_len = (size_t)n < sizeof(line) - 1 ? (size_t)n : sizeof(line) - 1;
        memcpy(line, buf, copy_len);
        line[copy_len] = '\0';
        size_t logical_len = copy_len;
        trim_line(line, &logical_len);
        char out[4096];
        int shutdown_flag = st->shutdown_requested;
        int out_len = server_process_line(line, logical_len, &st->stats, &shutdown_flag, out, sizeof(out));
        if (!st->shutdown_requested && shutdown_flag) {
            st->shutdown_requested = 1;
            log_info("shutdown requested by udp");
        }
        if (out_len > 0) {
            sendto(st->udp_fd, out, (size_t)out_len, 0, (struct sockaddr *)&addr, alen);
        }
    }
}

int server_run(const struct server_config *cfg) {
    if (!cfg) return -1;
    struct server_state st;
    memset(&st, 0, sizeof(st));
    st.client_buf_size = cfg->client_buffer_size ? cfg->client_buffer_size : 4096;
    st.max_clients = cfg->max_clients;
    st.epfd = epoll_create1(0);
    if (st.epfd == -1) {
        perror("epoll_create1");
        return -1;
    }
    int backlog = cfg->listen_backlog > 0 ? cfg->listen_backlog : 128;
    st.tcp_listen_fd = setup_tcp_listener(cfg->port, backlog);
    if (st.tcp_listen_fd == -1) {
        close(st.epfd);
        return -1;
    }
    st.udp_fd = setup_udp_socket(cfg->port);
    if (st.udp_fd == -1) {
        close(st.tcp_listen_fd);
        close(st.epfd);
        return -1;
    }
    if (add_fd_epoll(st.epfd, st.tcp_listen_fd, EPOLLIN) == -1) {
        perror("epoll add tcp listen");
        close(st.udp_fd);
        close(st.tcp_listen_fd);
        close(st.epfd);
        return -1;
    }
    if (add_fd_epoll(st.epfd, st.udp_fd, EPOLLIN) == -1) {
        perror("epoll add udp");
        close(st.udp_fd);
        close(st.tcp_listen_fd);
        close(st.epfd);
        return -1;
    }
    int max_events = cfg->max_events > 0 ? cfg->max_events : 64;
    struct epoll_event *events = calloc((size_t)max_events, sizeof(struct epoll_event));
    if (!events) {
        close(st.udp_fd);
        close(st.tcp_listen_fd);
        close(st.epfd);
        return -1;
    }
    log_info("server started on port %d", cfg->port);
    int rc = 0;
    while (!st.shutdown_requested) {
        int n = epoll_wait(st.epfd, events, max_events, -1);
        if (n == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            rc = -1;
            break;
        }
        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;
            if (ev & (EPOLLERR | EPOLLHUP)) {
                if (fd == st.tcp_listen_fd || fd == st.udp_fd) {
                    log_error("fatal error on fd=%d", fd);
                    st.shutdown_requested = 1;
                    rc = -1;
                    break;
                } else {
                    struct client *c = find_client(&st, fd);
                    if (c) close_client(&st, c);
                    continue;
                }
            }
            if (fd == st.tcp_listen_fd) {
                handle_tcp_accept(&st);
            } else if (fd == st.udp_fd) {
                handle_udp(&st);
            } else {
                if (ev & EPOLLIN) handle_tcp_client(&st, fd);
            }
            if (st.shutdown_requested) break;
        }
    }
    for (size_t i = 0; i < st.clients_count; i++) {
        if (st.clients[i].alive) {
            epoll_ctl(st.epfd, EPOLL_CTL_DEL, st.clients[i].fd, NULL);
            close(st.clients[i].fd);
        }
        free(st.clients[i].buf);
    }
    free(st.clients);
    free(events);
    close(st.udp_fd);
    close(st.tcp_listen_fd);
    close(st.epfd);
    log_info("server stopped total_tcp_clients=%" PRIu64 " current_tcp_clients=%" PRIu64 " total_udp_messages=%" PRIu64,
             st.stats.total_tcp_clients,
             st.stats.current_tcp_clients,
             st.stats.total_udp_messages);
    return rc;
}
