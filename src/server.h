#ifndef ASYNC_SERVER_SERVER_H
#define ASYNC_SERVER_SERVER_H

#include <stddef.h>
#include <stdint.h>

#include "event_loop.h"

typedef struct connection {
    int fd;
    char *buffer;
    size_t buffer_capacity;
    size_t buffered_bytes;
    size_t write_offset;
} connection;

enum connection_action {
    CONNECTION_READ,
    CONNECTION_WRITE,
    CONNECTION_CLOSE,
};

int server_create_listener(const char *bind_address, uint16_t port, int backlog);

connection *connection_create(int fd, size_t buffer_capacity);
void connection_destroy(event_loop *loop, connection *client);

enum connection_action connection_read(connection *client);
enum connection_action connection_write(connection *client);
int connection_update_interest(event_loop *loop, connection *client,
                               enum connection_action action);

int server_accept_connections(event_loop *loop, int listener_fd,
                              size_t buffer_capacity);

#endif
