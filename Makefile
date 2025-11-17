CC=gcc
CFLAGS=-Wall -Wextra -Wpedantic -std=c11 -O2 -g
LDFLAGS=
SRCDIR=src

SERVER_SRCS=$(SRCDIR)/main.c $(SRCDIR)/server.c
SERVER_OBJS=$(SERVER_SRCS:.c=.o)

TESTS_SRCS=$(SRCDIR)/tests.c $(SRCDIR)/server.c
TESTS_OBJS=$(TESTS_SRCS:.c=.o)

STRESS_SRCS=$(SRCDIR)/stress.c
STRESS_OBJS=$(STRESS_SRCS:.c=.o)

.PHONY=all clean

all: server tests stress

server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $(SERVER_OBJS) $(LDFLAGS)

tests: $(TESTS_OBJS)
	$(CC) $(CFLAGS) -o $@ $(TESTS_OBJS) $(LDFLAGS)

stress: $(STRESS_OBJS)
	$(CC) $(CFLAGS) -o $@ $(STRESS_OBJS) -lpthread $(LDFLAGS)

$(SRCDIR)/%.o: $(SRCDIR)/%.c $(SRCDIR)/server.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f server tests stress $(SRCDIR)/*.o
