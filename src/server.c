#include "server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define CONNECTION_EVENTS (EPOLLET | EPOLLONESHOT | EPOLLRDHUP)

static void log_errno(const char *operation) {
    fprintf(stderr, "%s: %s\n", operation, strerror(errno));
}

int server_create_listener(const char *bind_address, uint16_t port, int backlog) {
    int listener_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listener_fd == -1) {
        log_errno("socket");
        return -1;
    }

    const int enable = 1;
    if (setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == -1) {
        log_errno("setsockopt(SO_REUSEADDR)");
        close(listener_fd);
        return -1;
    }

    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
    };

    if (inet_pton(AF_INET, bind_address, &address.sin_addr) != 1) {
        if (errno == 0) {
            fprintf(stderr, "invalid IPv4 bind address: %s\n", bind_address);
        } else {
            log_errno("inet_pton");
        }
        close(listener_fd);
        return -1;
    }

    if (bind(listener_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        log_errno("bind");
        close(listener_fd);
        return -1;
    }

    if (listen(listener_fd, backlog) == -1) {
        log_errno("listen");
        close(listener_fd);
        return -1;
    }

    return listener_fd;
}

connection *connection_create(int fd, size_t buffer_capacity) {
    connection *client = calloc(1, sizeof(*client));
    if (client == NULL) {
        return NULL;
    }

    client->buffer = malloc(buffer_capacity);
    if (client->buffer == NULL) {
        free(client);
        return NULL;
    }

    client->fd = fd;
    client->buffer_capacity = buffer_capacity;
    return client;
}

void connection_destroy(event_loop *loop, connection *client) {
    if (client == NULL) {
        return;
    }

    if (loop != NULL && client->fd >= 0) {
        if (event_loop_remove(loop, client->fd) == -1 && errno != ENOENT && errno != EBADF) {
            log_errno("epoll_ctl(EPOLL_CTL_DEL)");
        }
    }

    if (client->fd >= 0) {
        close(client->fd);
    }
    free(client->buffer);
    free(client);
}

enum connection_action connection_read(connection *client) {
    while (client->buffered_bytes < client->buffer_capacity) {
        size_t available = client->buffer_capacity - client->buffered_bytes;
        ssize_t received = read(client->fd, client->buffer + client->buffered_bytes, available);

        if (received > 0) {
            client->buffered_bytes += (size_t)received;
            continue;
        }

        if (received == 0) {
            return client->buffered_bytes == 0 ? CONNECTION_CLOSE : CONNECTION_WRITE;
        }

        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return client->buffered_bytes == 0 ? CONNECTION_READ : CONNECTION_WRITE;
        }

        log_errno("read");
        return CONNECTION_CLOSE;
    }

    return CONNECTION_WRITE;
}

enum connection_action connection_write(connection *client) {
    while (client->write_offset < client->buffered_bytes) {
        size_t remaining = client->buffered_bytes - client->write_offset;
        ssize_t written = write(client->fd, client->buffer + client->write_offset, remaining);

        if (written > 0) {
            client->write_offset += (size_t)written;
            continue;
        }

        if (written == -1 && errno == EINTR) {
            continue;
        }
        if (written == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return CONNECTION_WRITE;
        }

        log_errno("write");
        return CONNECTION_CLOSE;
    }

    client->buffered_bytes = 0;
    client->write_offset = 0;
    return CONNECTION_READ;
}

int connection_update_interest(event_loop *loop, connection *client,
                               enum connection_action action) {
    uint32_t events = CONNECTION_EVENTS;

    switch (action) {
    case CONNECTION_READ:
        events |= EPOLLIN;
        break;
    case CONNECTION_WRITE:
        events |= EPOLLOUT;
        break;
    case CONNECTION_CLOSE:
        return 0;
    }

    return event_loop_modify(loop, client->fd, client, events);
}

int server_accept_connections(event_loop *loop, int listener_fd,
                              size_t buffer_capacity) {
    for (;;) {
        int client_fd = accept4(listener_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client_fd >= 0) {
            connection *client = connection_create(client_fd, buffer_capacity);
            if (client == NULL) {
                log_errno("allocate connection");
                close(client_fd);
                continue;
            }

            if (event_loop_add(loop, client_fd, client, CONNECTION_EVENTS | EPOLLIN) == -1) {
                log_errno("epoll_ctl(EPOLL_CTL_ADD)");
                connection_destroy(NULL, client);
            }
            continue;
        }

        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        if (errno == ECONNABORTED || errno == EPROTO || errno == ENETDOWN ||
            errno == ENOPROTOOPT || errno == EHOSTDOWN || errno == ENONET ||
            errno == EHOSTUNREACH || errno == EOPNOTSUPP || errno == ENETUNREACH) {
            continue;
        }

        log_errno("accept4");
        return -1;
    }
}
