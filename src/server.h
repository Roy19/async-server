#ifndef _SERVER_H
#define _SERVER_H

#include "event_loop.h"

int create_socket_and_listen();
int set_non_blocking(int fd);
event_state read_from_socket(event_data* ed);
event_state write_to_socket(event_data* ed);
void keep_accepting_connections(event_loop *el, int sockfd);
int event_loop_add_fd(event_loop *el, int fd, uint32_t events);
int event_loop_modify_fd(event_loop *el, int fd, event_data *ed, uint32_t events);
int event_loop_delete_fd(event_loop *el, int fd);

#endif