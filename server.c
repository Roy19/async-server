#include "server.h"

bool is_end(char *buffer, ssize_t last) {
    return (buffer[last - 1] == '\n' && 
            buffer[last - 2] == '\r' && 
            buffer[last - 3] == '\n' && 
            buffer[last - 4] == '\r');
}

void server(event_data* ed) {
    if (ed->state == READING) {
        while (1) {
            ssize_t bytes_read = read(ed->fd, (ed->incoming_data) + (ed->readn), 1);

            if (bytes_read == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                } else {
                    fprintf(stderr, "Failed to read from server. Errno:%d\n", errno);
                    ed->state = ENDED;
                    break;
                }
            } else if (bytes_read == 0) {
                break;
            } else {
                (ed->readn)++;
                if (ed->readn >= 4 && is_end(ed->incoming_data, ed->readn)) {
                    fprintf(stdout, "Read %ld bytes from client\n", bytes_read);
                    ed->state = WRITING;
                    break;
                }
            }
        }
    } else if (ed->state == WRITING) {
        size_t total_size_of_data = strlen(response_message), to_write = total_size_of_data;
        ssize_t bytes_written = 0, curr_pointer = 0;
        while (1) {
            bytes_written += write(ed->fd, response_message + curr_pointer, to_write);
            curr_pointer += bytes_written;

            if (bytes_written == -1) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    break;
                } else {
                    fprintf(stderr, "Failed to write to client\n");
                    break;
                }
            } else if (bytes_written == total_size_of_data) {
                fprintf(stdout, "Written all bytes to client\n");
                ed->state = ENDED;
                break;
            } else {
                fprintf(stdout, "Written %ld bytes to client\n", bytes_written);
                to_write -= bytes_written;
            }
        }
    }
}

int epoll_ctl_add_fd(int epoll_fd, int fd, uint32_t events) {
    struct epoll_event ev;
    event_data *data = (event_data *)malloc(sizeof(event_data));
    data->fd = fd;
    data->state = READING;
    data->readn = 0;
    data->incoming_data = (char *)malloc(buff_size);
    ev.data.ptr = data;
    ev.events = events;
    return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
}

int set_non_blocking(int fd) {
    int existing_flags = fcntl(fd, F_GETFL);
    if (existing_flags == -1) {
        fprintf(stderr, "Failed to get flags for socket\n");
        return -1;
    }
    return fcntl(fd, F_SETFL, existing_flags | O_NONBLOCK);
}

int main() {
    struct sockaddr_in server_address, client_address;
    struct epoll_event events[MAXEVENTS];
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        fprintf(stderr, "Failed to get socket\n");
        return -1;
    }

    if (set_non_blocking(sockfd) == -1) {
        fprintf(stderr, "Failed to set socket as non-blocking\n");
        close(sockfd);
        return -1;
    }

    bzero(&server_address, sizeof(server_address));

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(PORT);

    if (bind(sockfd, (struct sockaddr*)&server_address, sizeof(server_address)) == -1) {
        fprintf(stderr, "Failed to bind socket to address\n");
        close(sockfd);
        return -1;
    }

    if (listen(sockfd, 500) == -1) {
        fprintf(stderr, "Failed to listen on socket\n");
        close(sockfd);
        return -1;
    }

    int epoll_fd = epoll_create(1);
    if (epoll_fd == -1) {
        fprintf(stderr, "Failed to open an epoll instance\n");
        close(sockfd);
        return -1;
    }
    if (epoll_ctl_add_fd(epoll_fd, sockfd, EPOLLIN | EPOLLOUT | EPOLLET) == -1) {
        fprintf(stderr, "Failed to add socket to epoll structure\n");
        close(epoll_fd);
        close(sockfd);
        return -1;
    }

    while(1) {
        int nfds = epoll_wait(epoll_fd, events, MAXEVENTS, -1);

        if (nfds == -1) {
            fprintf(stderr, "Received wrong number of events\n");
            close(sockfd);
            close(epoll_fd);
            return -1;
        } else if (nfds == 0) {
            fprintf(stdout, "Received 0 events\n");
        }

        for (int i = 0; i < nfds; i++) {
            if (((event_data *)(events[i].data.ptr))->fd == sockfd) {
                // handle a new connection
                while (1) {
                    socklen_t length_of_address = sizeof(client_address);
                    int connfd = accept(sockfd, (struct sockaddr *)&client_address, &length_of_address);
                    if (connfd == -1) {
                        if (errno == EWOULDBLOCK || errno == EAGAIN) {
                            fprintf(stdout, "No pending connections\n");
                            break;
                        } else {
                            fprintf(stderr, "Failed to accept incoming connections\n");
                            close(sockfd);
                            return -1;
                        }
                    } else {
                        fprintf(stdout, "Got a new connection from a client\n");
                        if (set_non_blocking(connfd) == -1) {
                            fprintf(stderr, "Failed to set socket as non-blocking\n");
                            close(sockfd);
                            return -1;
                        }
                        epoll_ctl_add_fd(epoll_fd, connfd, EPOLLIN | EPOLLET | EPOLLONESHOT);
                    }
                }
            } else {
                struct epoll_event ev;
                event_data *ed = (event_data *)(events[i].data.ptr);
                server(ed);

                if (ed->state == READING) {
                    ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                    ev.data.ptr = ed;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, ed->fd, &ev) == -1) {
                        fprintf(stderr, "Failed to modify file descriptor");
                        continue;
                    }
                } else if (ed->state == WRITING) {
                    ev.events = EPOLLOUT | EPOLLET | EPOLLONESHOT;
                    ev.data.ptr = ed;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, ed->fd, &ev) == -1) {
                        fprintf(stderr, "Failed to modify file descriptor");
                        continue;
                    }
                } else {
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ed->fd, NULL);
                    close(ed->fd);
                    free(ed->incoming_data);
                    free(ed);
                }
            }

            if (events[i].events & (EPOLLHUP | EPOLLRDHUP)) {
                fprintf(stdout, "Client connection closed\n");
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
                close(events[i].data.fd);
            }
        }
    }
    
    close(epoll_fd);
    close(sockfd);

    return 0;
}