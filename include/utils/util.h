#ifndef UTIL_H_
#define UTIL_H_

#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <unistd.h>

void errif(bool, const char*);

int setnonblocking( int fd );

void addsig(int sig, void(handler)(int), bool restart = true);

void addfd(int epollfd, int fd, bool one_shot);

/* 从epollfd标识的内核事件表中删除fd上的所有注册事件 */
void removefd(int epollfd, int fd);

void modfd(int epollfd, int fd, int ev);

void show_error(int connfd, const char* info);

class Utils
{
public:
    Utils() {}
    ~Utils() {}

    //信号处理函数
    static void sig_handler(int sig);
public:
    static int *u_pipefd;
};

#endif
