#include <stdio.h>
#include <unistd.h>
#include "server.h"

void add_to_event_loop_based_on_new_state(event_loop* el, event_data* ed, event_state state) {
    if (state == READING) {
        event_loop_modify_fd(el, ed->fd, ed, EPOLLIN | EPOLLET | EPOLLONESHOT);
    } else if (state == WRITING) {
        event_loop_modify_fd(el, ed->fd, ed, EPOLLOUT | EPOLLET | EPOLLONESHOT);
    } else {
        event_loop_delete_fd(el, ed->fd);
    }
}

int main(int argc, char **argv) {
    
    int sockfd = create_socket_and_listen();
    if (sockfd == -1) {
        fprintf(stderr, "Failed to create socket\n");
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
                event_state new_state;
                if (ed->state == READING) {
                    new_state = read_from_socket(ed);
                } else if (ed->state == WRITING) {
                    new_state = write_to_socket(ed);
                } else {
                    free(ed->incoming_data);
                    close(ed->fd);
                    new_state = CLOSED;
                }

                add_to_event_loop_based_on_new_state(el, ed, new_state);
            }
        }
    }
    
    destroy_event_loop(el);
    close(sockfd);

    return 0;
}