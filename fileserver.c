#define _GNU_SOURCE
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

const int buff_size = 1024 * 1024;
const int max_bytes = 8 * 1024 * 1024;
const int PORT = 8080;
const char* localhost = "127.0.0.1";
const char* response_message = "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: 251582294\r\n\r\n";

off_t get_file_size(int fd)
{
    struct stat buf;
    fstat(fd, &buf);
    return buf.st_size;
}

void do_file_copy(int fd1, int fd2)
{
    ssize_t nr = 0, nw = 0;

    int pipefd[2];

    if (pipe(pipefd) == -1)
    {
        fprintf(stderr, "Could not create pipes\n");
        return;
    }
    ssize_t file_size = get_file_size(fd1), to_read = file_size;

    while (to_read > 0)
    {
        nr = splice(fd1, NULL, pipefd[1], NULL, max_bytes, SPLICE_F_MOVE | SPLICE_F_MORE);
        if (nr < 0)
        {
            fprintf(stderr, "Could not write to pipe from input file\n");
            close(pipefd[0]);
            close(pipefd[1]);
            return;
        }

        nw = splice(pipefd[0], NULL, fd2, NULL, nr, SPLICE_F_MOVE | SPLICE_F_MORE);
        if (nw < 0)
        {
            fprintf(stderr, "Could not write to output file from input pipe\n");
            close(pipefd[0]);
            close(pipefd[1]);
            return;
        }
        to_read -= nw;
    }

    close(pipefd[0]);
    close(pipefd[1]);
}

void do_file_copy_v2(int fd, int connection_fd)
{
    ssize_t bytes_read = 0, bytes_written = 0;
    while (1)
    {
        char buff[buff_size];
        bytes_read = read(fd, buff, buff_size);
        if (bytes_read <= 0)
        {
            break;
        }
        bytes_written = write(connection_fd, buff, buff_size);
        if (bytes_written == -1)
        {
            fprintf(stderr, "Failed to write to socket\n");
            break;
        }
    }
}

void send_response_back(int connection_fd) {
    char buffer[1024];
    ssize_t bytes_read = read(connection_fd, buffer, sizeof(buffer));

    // if (bytes_read == -1) {
    //     fprintf(stderr, "Failed to read from connection\n");
    // } else {
    //     fprintf(stdout, "Read %ld bytes\n", bytes_read);
    // }

    ssize_t bytes_written = write(connection_fd, response_message, strlen(response_message));

    if (bytes_written == -1) {
        fprintf(stderr, "Failed to write to connection\n");
    } else {
        int fd = open("testimage1.png", O_RDONLY);

        if (fd == -1) {
            fprintf(stderr, "Failed to open file for reading\n");
        } else {
            do_file_copy(fd, connection_fd);
            close(fd);
        }
    }
}

int main() {
    struct sockaddr_in server_address, client_address;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
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

    if (listen(sockfd, 5000) == -1) {
        fprintf(stderr, "Failed to listen on socket\n");
        close(sockfd);
        return -1;
    }

    while(1) {
        socklen_t length_of_address = sizeof(client_address);
        int connfd = accept(sockfd, (struct sockaddr*)&client_address, &length_of_address);
        if (connfd == -1) {
            fprintf(stderr, "Failed to accept incoming connections\n");
            close(sockfd);
            return -1;
        } else {
            fprintf(stdout, "Got a new connection from a client\n");
            send_response_back(connfd);
            close(connfd);
        }
    }
    
    close(sockfd);

    return 0;
}