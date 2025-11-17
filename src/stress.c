#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

struct worker_args {
    const char *host;
    int port;
    int messages;
    int id;
};

static void *worker_thread(void *arg) {
    struct worker_args *wa = (struct worker_args *)arg;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) pthread_exit(NULL);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)wa->port);
    if (inet_pton(AF_INET, wa->host, &addr.sin_addr) != 1) {
        close(fd);
        pthread_exit(NULL);
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        close(fd);
        pthread_exit(NULL);
    }
    char buf[1024];
    for (int i = 0; i < wa->messages; i++) {
        int type = i % 4;
        if (type == 0) {
            snprintf(buf, sizeof(buf), "hello-%d-%d\n", wa->id, i);
        } else if (type == 1) {
            snprintf(buf, sizeof(buf), "/time\n");
        } else if (type == 2) {
            snprintf(buf, sizeof(buf), "/stats\n");
        } else {
            snprintf(buf, sizeof(buf), "/help\n");
        }
        size_t len = strlen(buf);
        size_t sent = 0;
        while (sent < len) {
            ssize_t n = send(fd, buf + sent, len - sent, 0);
            if (n == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                close(fd);
                pthread_exit(NULL);
            }
            sent += (size_t)n;
        }
        ssize_t r = recv(fd, buf, sizeof(buf) - 1, 0);
        if (r <= 0) break;
        buf[r] = '\0';
    }
    close(fd);
    pthread_exit(NULL);
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s host port threads messages_per_thread\n", argv[0]);
        return 1;
    }
    const char *host = argv[1];
    int port = atoi(argv[2]);
    int threads = atoi(argv[3]);
    int messages = atoi(argv[4]);
    if (port <= 0 || port > 65535 || threads <= 0 || messages <= 0) {
        fprintf(stderr, "invalid arguments\n");
        return 1;
    }
    pthread_t *t = malloc((size_t)threads * sizeof(pthread_t));
    struct worker_args *args = malloc((size_t)threads * sizeof(struct worker_args));
    if (!t || !args) {
        free(t);
        free(args);
        return 1;
    }
    for (int i = 0; i < threads; i++) {
        args[i].host = host;
        args[i].port = port;
        args[i].messages = messages;
        args[i].id = i;
        pthread_create(&t[i], NULL, worker_thread, &args[i]);
    }
    for (int i = 0; i < threads; i++) {
        pthread_join(t[i], NULL);
    }
    free(t);
    free(args);
    return 0;
}
