#ifndef _EVENT_LOOP
#define _EVENT_LOOP

#include <sys/epoll.h>
#include "event_data.h"

#define MAX_EVENTS 8192

typedef struct event_loop {
    int event_loop_fd;
    struct epoll_event *events;
} event_loop;

event_loop *create_event_loop();
int add_to_event_loop(event_loop *el, int fd, event_data *ed, uint32_t events);
int modify_file_descriptor_in_event_loop(event_loop *el, int fd, event_data *ed, uint32_t events);
int remove_from_event_loop(event_loop *el, int fd);
void destroy_event_loop(event_loop *el);
int wait_for_events(event_loop *el, int timeout);

#endif