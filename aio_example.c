#include <stdlib.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <aio.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>


static int setup_aio(int fd)
{
    int err;
    int sigfd;
    char *buf;
    sigset_t mask;
    struct signalfd_siginfo ssi;

    struct aiocb *aio = NULL;
    struct sigevent sigev = {
        .sigev_notify = SIGEV_SIGNAL,
        .sigev_signo  = SIGRTMIN + (fd % 32) 
    };
    

    buf = malloc(sizeof(char) * 1024);
    memset(buf, 0x0, sizeof(char) * 1024);

    aio = malloc(sizeof(struct aiocb));
    memset(aio, 0x0, sizeof(struct aiocb));

    printf("alloc aiocb %p\n", aio);

    aio->aio_buf = buf;
    aio->aio_nbytes = sizeof(buf);
    aio->aio_reqprio = 0;
    aio->aio_sigevent = sigev; 
    
    printf("aio fd:%d\n", fd);

    aio->aio_fildes = fd;
    assert(aio->aio_fildes >= 0);
    aio->aio_sigevent.sigev_value.sival_ptr = aio;
    
    sigemptyset(&mask);
    sigaddset(&mask, aio->aio_sigevent.sigev_signo);
    err = sigprocmask(SIG_BLOCK, &mask, NULL);
    assert(!err);
    sigfd = signalfd(-1, &mask, SFD_NONBLOCK|SFD_CLOEXEC);

    err = aio_read(aio);
    assert(!err);
    
    return sigfd;
    
}


static int add(int epfd, int fd)
{
    int err;
    struct epoll_event ev;

    ev.data.fd = setup_aio(fd);
    assert(ev.data.fd >= 0);

    ev.events = EPOLLIN /* | EPOLLET */;
    err = epoll_ctl(epfd, EPOLL_CTL_ADD, ev.data.fd, &ev);
    assert(!err);

    printf("add sigfd:%d\n", ev.data.fd);
    return ev.data.fd;
}

int main(int argc, char *argv[])
{
    int err, fd, sigfd;
    ssize_t ssz;
    int epfd, i, sock, nfd;
    int maxopen = 1024;
    struct signalfd_siginfo ssi;
    struct aiocb *temp;


    epfd = epoll_create(1);
    assert(!epfd >= 0);

    fd = open(argv[1], O_RDONLY);
    printf("open fd:%d\n", fd);
    sigfd = add(epfd, fd);
    fd = open(argv[2], O_RDONLY);

    printf("open fd:%d\n", fd);
    sigfd = add(epfd, fd);

    while (1) {
        struct epoll_event e[maxopen];

        nfd = epoll_wait(epfd, e, maxopen, 1 * 1000);
        if (nfd == 0) {
            return;
        }

        for (i = 0; i < nfd; i++) {
            sock = e[i].data.fd;
            printf("ev:%d\n", sock);

            if (e[i].events & EPOLLIN) {

                err = read(sock, &ssi, sizeof(ssi));
                if (err == -1) {
                    continue;
                }

                temp = (struct aiocb*)ssi.ssi_ptr;
                printf("notify aiocb %p\n", temp);

                err = aio_error(temp);
                printf("aio_error:%d\n", err);            

                if (err == EINPROGRESS) {
                    continue;
                }

                ssz = aio_return(temp);
                printf("%.*s\n", (int)ssz, (char*)temp->aio_buf);

                /* printf("%s: aio_read from fd %d completed\n", __func__, ssi.ssi_ptr); */
                free((void*)temp->aio_buf);
                free(temp);

            }
        }
    }
    return 0;
}
