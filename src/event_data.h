#ifndef _EVENT_DATA
#define _EVENT_DATA

#include <stdlib.h>

typedef enum fd_state {
    READING,
    WRITING,
    ENDED
}fd_state;


typedef struct event_data {
    fd_state state;
    int fd;
    char *incoming_data;
    ssize_t readn;
}event_data;

#endif