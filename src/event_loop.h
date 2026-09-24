#ifndef ASYNC_SERVER_EVENT_LOOP_H
#define ASYNC_SERVER_EVENT_LOOP_H

#include <stddef.h>
#include <stdint.h>
#include <sys/epoll.h>

#define MAX_EVENTS 8192

typedef struct event_loop {
    int fd;
    struct epoll_event *events;
    size_t max_events;
} event_loop;

event_loop *event_loop_create(size_t max_events);
void event_loop_destroy(event_loop *loop);

int event_loop_add(event_loop *loop, int fd, void *data, uint32_t events);
int event_loop_modify(event_loop *loop, int fd, void *data, uint32_t events);
int event_loop_remove(event_loop *loop, int fd);
int event_loop_wait(event_loop *loop, int timeout_ms);

#endif
