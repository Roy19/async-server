#ifndef _SERVER_H
#define _SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/epoll.h>
#include <stdbool.h>

const int buff_size = 1024 * 1024;
const int max_bytes = 8 * 1024 * 1024;
const int PORT = 8080;
const int MAXEVENTS = 10000;
const char* localhost = "127.0.0.1";
const char* response_message = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\nHello World!";

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