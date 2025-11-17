#include "server.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static void test_echo_simple(void) {
    struct server_stats stats;
    memset(&stats, 0, sizeof(stats));
    int shutdown = 0;
    char out[128];
    const char *msg = "hello";
    int n = server_process_line(msg, strlen(msg), &stats, &shutdown, out, sizeof(out));
    assert(n == (int)strlen(msg) + 1);
    out[n] = '\0';
    assert(strcmp(out, "hello\n") == 0);
    assert(shutdown == 0);
}

static void test_echo_trim_spaces(void) {
    struct server_stats stats;
    memset(&stats, 0, sizeof(stats));
    int shutdown = 0;
    char out[128];
    const char *msg = "   abc  ";
    int n = server_process_line(msg, strlen(msg), &stats, &shutdown, out, sizeof(out));
    out[n] = '\0';
    assert(strcmp(out, "abc\n") == 0);
}

static void test_empty_line(void) {
    struct server_stats stats;
    memset(&stats, 0, sizeof(stats));
    int shutdown = 0;
    char out[16];
    int n = server_process_line("", 0, &stats, &shutdown, out, sizeof(out));
    assert(n == 0);
    const char *msg = "   \n";
    n = server_process_line(msg, strlen(msg), &stats, &shutdown, out, sizeof(out));
    assert(n == 0);
}

static void test_time_format(void) {
    struct server_stats stats;
    memset(&stats, 0, sizeof(stats));
    int shutdown = 0;
    char out[128];
    const char *cmd = "/time";
    int n = server_process_line(cmd, strlen(cmd), &stats, &shutdown, out, sizeof(out));
    assert(n > 0);
    assert(n >= 20);
    assert(isdigit((unsigned char)out[0]));
    assert(isdigit((unsigned char)out[1]));
    assert(isdigit((unsigned char)out[2]));
    assert(isdigit((unsigned char)out[3]));
    assert(out[4] == '-');
    assert(out[7] == '-');
    assert(out[10] == ' ');
    assert(out[13] == ':');
    assert(out[16] == ':');
}

static void test_stats_output(void) {
    struct server_stats stats;
    memset(&stats, 0, sizeof(stats));
    stats.total_tcp_clients = 10;
    stats.current_tcp_clients = 3;
    stats.total_udp_messages = 5;
    int shutdown = 0;
    char out[256];
    int n = server_process_line("/stats", strlen("/stats"), &stats, &shutdown, out, sizeof(out));
    assert(n > 0);
    out[n] = '\0';
    assert(strstr(out, "total_tcp_clients=10") != NULL);
    assert(strstr(out, "current_tcp_clients=3") != NULL);
    assert(strstr(out, "total_udp_messages=5") != NULL);
}

static void test_help_output(void) {
    struct server_stats stats;
    memset(&stats, 0, sizeof(stats));
    int shutdown = 0;
    char out[256];
    int n = server_process_line("/help", strlen("/help"), &stats, &shutdown, out, sizeof(out));
    assert(n > 0);
    out[n] = '\0';
    assert(strstr(out, "/time") != NULL);
    assert(strstr(out, "/stats") != NULL);
    assert(strstr(out, "/shutdown") != NULL);
    assert(strstr(out, "/help") != NULL);
}

static void test_shutdown_flag(void) {
    struct server_stats stats;
    memset(&stats, 0, sizeof(stats));
    int shutdown = 0;
    char out[64];
    int n = server_process_line("/shutdown", strlen("/shutdown"), &stats, &shutdown, out, sizeof(out));
    assert(n > 0);
    out[n] = '\0';
    assert(shutdown == 1);
    assert(strcmp(out, "shutting down\n") == 0);
}

static void test_unknown_command(void) {
    struct server_stats stats;
    memset(&stats, 0, sizeof(stats));
    int shutdown = 0;
    char out[64];
    int n = server_process_line("/foobar", strlen("/foobar"), &stats, &shutdown, out, sizeof(out));
    assert(n > 0);
    out[n] = '\0';
    assert(strcmp(out, "unknown command\n") == 0);
    assert(shutdown == 0);
}

static void test_small_buffer_failure(void) {
    struct server_stats stats;
    memset(&stats, 0, sizeof(stats));
    stats.total_tcp_clients = 1;
    stats.current_tcp_clients = 1;
    stats.total_udp_messages = 1;
    int shutdown = 0;
    char out[8];
    int n = server_process_line("/stats", strlen("/stats"), &stats, &shutdown, out, sizeof(out));
    assert(n == -1);
}

int main(void) {
    test_echo_simple();
    test_echo_trim_spaces();
    test_empty_line();
    test_time_format();
    test_stats_output();
    test_help_output();
    test_shutdown_flag();
    test_unknown_command();
    test_small_buffer_failure();
    printf("all tests passed\n");
    return 0;
}
