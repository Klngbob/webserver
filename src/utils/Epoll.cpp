#include <unistd.h>
#include <string.h>
#include "utils/util.h"
#include "utils/Epoll.h"

#define MAX_EVENTS 100000

Epoll::Epoll(): epfd(-1), events(nullptr)
{
    epfd = epoll_create(5);
    errif(epfd == -1, "epoll create error");
    events = new epoll_event[MAX_EVENTS];
    bzero(events, sizeof(*events) * MAX_EVENTS);
}

Epoll::~Epoll()
{
    if(epfd != -1)
    {
        close(epfd);
        epfd = -1;
    }
    delete[] events;
}

void Epoll::addFd(int fd, uint32_t op)
{
    struct epoll_event ev;
    bzero(&ev, sizeof(ev));
    ev.data.fd = fd;
    ev.events = op;
    errif(epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == -1, 
            "epoll add event error");
}

int Epoll::getFd()
{
    return epfd;
}

std::vector<epoll_event> Epoll::poll(int timeout)
{
    std::vector<epoll_event> activeEvents;
    int nfds = epoll_wait(epfd, events, MAX_EVENTS, timeout);
    errif(nfds == -1 && errno != EINTR, "epoll wait error");
    for(int i = 0; i < nfds; ++i)
    {
        activeEvents.push_back(events[i]);
    }
    return activeEvents;
}