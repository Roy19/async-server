#include "event_loop.h"

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

event_loop *event_loop_create(size_t max_events) {
    if (max_events == 0) {
        errno = EINVAL;
        return NULL;
    }

    event_loop *loop = calloc(1, sizeof(*loop));
    if (loop == NULL) {
        return NULL;
    }

    loop->events = calloc(max_events, sizeof(*loop->events));
    if (loop->events == NULL) {
        free(loop);
        return NULL;
    }

    loop->fd = epoll_create1(EPOLL_CLOEXEC);
    if (loop->fd == -1) {
        free(loop->events);
        free(loop);
        return NULL;
    }

    loop->max_events = max_events;
    return loop;
}

void event_loop_destroy(event_loop *loop) {
    if (loop == NULL) {
        return;
    }

    if (loop->fd >= 0) {
        close(loop->fd);
    }
    free(loop->events);
    free(loop);
}

static int event_loop_control(event_loop *loop, int operation, int fd, void *data,
                              uint32_t events) {
    if (loop == NULL || loop->fd < 0) {
        errno = EINVAL;
        return -1;
    }

    struct epoll_event event = {
        .events = events,
        .data.ptr = data,
    };

    return epoll_ctl(loop->fd, operation, fd, &event);
}

int event_loop_add(event_loop *loop, int fd, void *data, uint32_t events) {
    return event_loop_control(loop, EPOLL_CTL_ADD, fd, data, events);
}

int event_loop_modify(event_loop *loop, int fd, void *data, uint32_t events) {
    return event_loop_control(loop, EPOLL_CTL_MOD, fd, data, events);
}

int event_loop_remove(event_loop *loop, int fd) {
    if (loop == NULL || loop->fd < 0) {
        errno = EINVAL;
        return -1;
    }

    return epoll_ctl(loop->fd, EPOLL_CTL_DEL, fd, NULL);
}

int event_loop_wait(event_loop *loop, int timeout_ms) {
    if (loop == NULL || loop->fd < 0 || loop->events == NULL || loop->max_events == 0) {
        errno = EINVAL;
        return -1;
    }

    return epoll_wait(loop->fd, loop->events, (int)loop->max_events, timeout_ms);
}
