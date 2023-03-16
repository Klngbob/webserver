#ifndef EPOLL_H_
#define EPOLL_H_

#include <sys/epoll.h>
#include <vector>

class Epoll
{
public:
    int epfd;
    struct epoll_event *events;
public:
    Epoll();
    ~Epoll();

    void addFd(int fd, uint32_t op);
    int getFd();
    std::vector<epoll_event> poll(int timeout = -1);
};

#endif