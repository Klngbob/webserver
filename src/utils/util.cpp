#include <stdio.h>
#include <stdlib.h>
#include "utils/util.h"

void errif(bool condition, const char* errmsg)
{
    if(condition)
    {
        perror(errmsg);
        exit(EXIT_FAILURE);
    }
}

int setnonblocking( int fd )
{
    int old_option = fcntl( fd, F_GETFL );
    int new_option = old_option | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_option );
    return old_option;
}

void addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if(restart)
    {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if(one_shot)
    {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

/* 从epollfd标识的内核事件表中删除fd上的所有注册事件 */
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

void show_error(int connfd, const char* info)
{
    // printf("Error: %s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

void Utils::sig_handler( int sig )
{
    int save_errno = errno;
    int msg = sig;
    send( u_pipefd[1], ( char* )&msg, 1, 0 );
    errno = save_errno;
}

int* Utils::u_pipefd = 0;