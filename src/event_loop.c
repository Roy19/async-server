#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "event_loop.h"

event_loop *create_event_loop() {
    event_loop *el = (event_loop *)malloc(sizeof(event_loop));
    if (el == NULL) {
        perror("Failed to create event loop");
        return NULL;
    }
    el->events = (struct epoll_event *)malloc(sizeof(struct epoll_event) * MAX_EVENTS);
    if (el->events == NULL) {
        perror("Failed to allocate memory for event loop");
        free(el);
        return NULL;
    }
    el->event_loop_fd = epoll_create(EPOLL_CLOEXEC);
    if (el->event_loop_fd == -1) {
        perror("Failed to create epoll based event loop");
        free(el->events);
        free(el);
        return NULL;
    }
    return el;
}

void destroy_event_loop(event_loop *el) {
    if (el == NULL) {
        perror("event loop specified is NULL");
    } else {
        if (el->events != NULL) {
            free(el->events);
        }
        if (el->event_loop_fd > 0) {
            close(el->event_loop_fd);
        }
        free(el);
    }
}

int add_to_event_loop(event_loop *el, int fd, event_data *ed, uint32_t events) {
    if (el == NULL || el->event_loop_fd < 0) {
        perror("Wrong event loop specified");
        return -1;
    }
    struct epoll_event ev;
    ev.data.ptr = ed;
    ev.events = events;
    return epoll_ctl(el->event_loop_fd, EPOLL_CTL_ADD, fd, &ev);
}

int modify_file_descriptor_in_event_loop(event_loop *el, int fd, event_data *ed, uint32_t events) {
    if (el == NULL || el->event_loop_fd < 0) {
        perror("Wrong event loop specified");
        return -1;
    }
    struct epoll_event ev;
    ev.data.ptr = ed;
    ev.events = events;
    return epoll_ctl(el->event_loop_fd, EPOLL_CTL_MOD, fd, &ev);
}

int remove_from_event_loop(event_loop *el, int fd) {
    if (el == NULL || el->event_loop_fd < 0) {
        perror("Wrong event loop specified");
        return -1;
    }
    return epoll_ctl(el->event_loop_fd, EPOLL_CTL_DEL, fd, NULL);
}

int wait_for_events(event_loop *el, int timeout) {
    if (el == NULL || el->events == NULL || el->event_loop_fd < 0) {
        perror("Wrong event loop specified");
        return -1;
    }
    return epoll_wait(el->event_loop_fd, el->events, MAX_EVENTS, timeout);
}