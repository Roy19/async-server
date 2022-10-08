#include "server.h"
#include "event_loop.h"

void server(event_data* ed) {
    ssize_t bytes_read = read(ed->fd, (ed->incoming_data) + (ed->readn), BUFFSIZE);
    if (bytes_read <= 0) {
        close(ed->fd);
        event_loop_delete_fd(ed->el, ed->fd);
        return;
    } else {
        (ed->readn) += bytes_read;
        // fprintf(stdout, "Read %ld bytes from client\n", ed->readn);
    }

    size_t total_size_of_data = ed->readn, to_write = total_size_of_data;
    ssize_t bytes_written = 0, curr_pointer = 0;
    while (1) {
        bytes_written += write(ed->fd, ed->incoming_data + curr_pointer, to_write);
        curr_pointer += bytes_written;
        if (bytes_written == -1) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                return;
            } else {
                // fprintf(stderr, "Failed to write to client\n");
                return;
            }
        } else if (bytes_written == total_size_of_data) {
            // fprintf(stdout, "Written all bytes to client\n");
            free(ed->incoming_data);
            ed->incoming_data = (char *)malloc(BUFFSIZE);
            ed->readn = 0;
            return;
        } else {
            // fprintf(stdout, "Written %ld bytes to client\n", bytes_written);
            to_write -= bytes_written;
        }
    }
}


int event_loop_add_fd(event_loop *el, int fd, uint32_t events) {
    event_data *data = (event_data *)malloc(sizeof(event_data));
    data->el = el;
    data->fd = fd;
    data->readn = 0;
    data->incoming_data = (char *)malloc(BUFFSIZE);
    return add_to_event_loop(el, fd, data, events);
}

int event_loop_modify_fd(event_loop *el, int fd, event_data *ed, uint32_t events) {
    return modify_file_descriptor_in_event_loop(el, fd, ed, events);
}

int event_loop_delete_fd(event_loop *el, int fd) {
    return remove_from_event_loop(el, fd);
}

int set_non_blocking(int fd) {
    int existing_flags = fcntl(fd, F_GETFL);
    if (existing_flags == -1) {
        // fprintf(stderr, "Failed to get flags for socket\n");
        return -1;
    }
    return fcntl(fd, F_SETFL, existing_flags | O_NONBLOCK);
}

void keep_accepting_connections(event_loop *el, int sockfd) {
    struct sockaddr_in client_address;
    while (1) {
        socklen_t length_of_address = sizeof(client_address);
        int connfd = accept(sockfd, (struct sockaddr *)&client_address, &length_of_address);
        if (connfd == -1) {
            return;
        } else {
            // fprintf(stdout, "Got a new connection from a client\n");
            if (set_non_blocking(connfd) == -1) {
                // fprintf(stderr, "Failed to set socket as non-blocking\n");
                return;
            }
            event_loop_add_fd(el, connfd, EPOLLIN | EPOLLET);
        }
    }
}

int main(int argc, char **argv) {
    struct sockaddr_in server_address;
    int sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (sockfd == -1) {
        fprintf(stderr, "Failed to get socket\n");
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

    if (listen(sockfd, BACKLOG) == -1) {
        fprintf(stderr, "Failed to listen on socket\n");
        close(sockfd);
        return -1;
    }

    event_loop *el = create_event_loop();
    if (el == NULL) {
        fprintf(stderr, "Failed to initialize the event loop");
        return -1;
    }

    if (event_loop_add_fd(el, sockfd, EPOLLIN) == -1) {
        fprintf(stderr, "Failed to add listening socket to event loop\n");
        destroy_event_loop(el);
        close(sockfd);
        return -1;
    }

    while(1) {
        int nfds = wait_for_events(el, -1);

        for (int i = 0; i < nfds; i++) {
            if (((event_data *)(el->events[i].data.ptr))->fd == sockfd) {
                // handle a new connection
                keep_accepting_connections(el, sockfd);
            } else {
                event_data *ed = (event_data *)(el->events[i].data.ptr);
                server(ed);
            }
        }
    }
    
    destroy_event_loop(el);
    close(sockfd);

    return 0;
}