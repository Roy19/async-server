#include "event_loop.h"
#include "server.h"

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define DEFAULT_BIND_ADDRESS "127.0.0.1"
#define DEFAULT_PORT 8080U
#define DEFAULT_BACKLOG 256
#define DEFAULT_BUFFER_CAPACITY (16U * 1024U)

static volatile sig_atomic_t stop_requested;

static void request_shutdown(int signal_number) {
    (void)signal_number;
    stop_requested = 1;
}

static int parse_port(const char *value, uint16_t *port) {
    char *end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(value, &end, 10);

    if (errno != 0 || value == end || *end != '\0' || parsed == 0 || parsed > UINT16_MAX) {
        return -1;
    }

    *port = (uint16_t)parsed;
    return 0;
}

static void print_usage(const char *program) {
    fprintf(stderr, "Usage: %s [bind-address] [port]\n", program);
}

int main(int argc, char **argv) {
    const char *bind_address = DEFAULT_BIND_ADDRESS;
    uint16_t port = DEFAULT_PORT;

    if (argc > 3) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (argc >= 2) {
        bind_address = argv[1];
    }
    if (argc == 3 && parse_port(argv[2], &port) == -1) {
        fprintf(stderr, "invalid port: %s\n", argv[2]);
        return EXIT_FAILURE;
    }

    struct sigaction shutdown_action = {
        .sa_handler = request_shutdown,
    };
    sigemptyset(&shutdown_action.sa_mask);
    if (sigaction(SIGINT, &shutdown_action, NULL) == -1 ||
        sigaction(SIGTERM, &shutdown_action, NULL) == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    int listener_fd = server_create_listener(bind_address, port, DEFAULT_BACKLOG);
    if (listener_fd == -1) {
        return EXIT_FAILURE;
    }

    event_loop *loop = event_loop_create(MAX_EVENTS);
    if (loop == NULL) {
        perror("create event loop");
        close(listener_fd);
        return EXIT_FAILURE;
    }

    if (event_loop_add(loop, listener_fd, NULL, EPOLLIN | EPOLLET) == -1) {
        perror("epoll_ctl(EPOLL_CTL_ADD listener)");
        event_loop_destroy(loop);
        close(listener_fd);
        return EXIT_FAILURE;
    }

    fprintf(stdout, "Echo server listening on %s:%" PRIu16 "\n", bind_address, port);

    int exit_status = EXIT_SUCCESS;
    while (!stop_requested) {
        int ready_count = event_loop_wait(loop, -1);
        if (ready_count == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("epoll_wait");
            exit_status = EXIT_FAILURE;
            break;
        }

        for (int index = 0; index < ready_count; ++index) {
            struct epoll_event event = loop->events[index];

            if (event.data.ptr == NULL) {
                if (server_accept_connections(loop, listener_fd, DEFAULT_BUFFER_CAPACITY) == -1) {
                    exit_status = EXIT_FAILURE;
                    stop_requested = 1;
                    break;
                }
                continue;
            }

            connection *client = event.data.ptr;
            enum connection_action next_action;

            if ((event.events & EPOLLERR) != 0) {
                next_action = CONNECTION_CLOSE;
            } else if ((event.events & EPOLLIN) != 0) {
                next_action = connection_read(client);
            } else if ((event.events & EPOLLOUT) != 0) {
                next_action = connection_write(client);
            } else {
                next_action = CONNECTION_CLOSE;
            }

            if (next_action == CONNECTION_CLOSE) {
                connection_destroy(loop, client);
                continue;
            }

            if (connection_update_interest(loop, client, next_action) == -1) {
                perror("epoll_ctl(EPOLL_CTL_MOD)");
                connection_destroy(loop, client);
            }
        }
    }

    event_loop_remove(loop, listener_fd);
    close(listener_fd);
    event_loop_destroy(loop);
    return exit_status;
}
