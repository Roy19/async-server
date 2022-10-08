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

const int BUFFSIZE = 1 * 1024;
const int PORT = 8080;
const int MAXEVENTS = 4096;
const int BACKLOG = 128;
const char* localhost = "127.0.0.1";

#endif