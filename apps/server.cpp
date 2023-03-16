#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <vector>
#include "util.h"
#include "Socket.h"
#include "Epoll.h"
#include "InetAddress.h"

#define NAX_EVENTS 1024
#define READ_BUFFER 1024

void setnonblocking(int fd)
{
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}

void handleReadEvent(int sockfd);

int main(int argc, char* argv[])
{
    if( argc <= 2 )
    {
        printf( "usage: %s ip_address port_number\n", basename( argv[0] ) );
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi( argv[2] );
    
    Socket* serv_sock = new Socket();
    InetAddress* serv_addr = new InetAddress(ip, port);

    serv_sock->bind(serv_addr);
    serv_sock->listen();

    Epoll* ep = new Epoll();
    serv_sock->setnonblocking();
    ep->addFd(serv_sock->getFd(), EPOLLIN | EPOLLET);
    while (true)
    {
        std::vector<epoll_event> events = ep->poll();
        int nfds = events.size();
        for(int i = 0; i < nfds; ++i)
        {
            int sockfd = events[i].data.fd;
            if(sockfd == serv_sock->getFd()) // 有新客户连接
            {
                InetAddress* clnt_addr = new InetAddress(); // 会发生内存泄漏
                Socket* clnt_sock = new Socket(serv_sock->accept(clnt_addr));
                clnt_sock->setnonblocking();
                ep->addFd(clnt_sock->getFd(), EPOLLIN | EPOLLET);
            }
            else if(events[i].events | EPOLLIN)
            {
                handleReadEvent(sockfd);
            }
            else
            {
                printf("something else happened\n");
            }
        }
    }
    delete serv_sock;
    delete serv_addr;
    return 0;
}

void handleReadEvent(int sockfd)
{
    char buf[READ_BUFFER];
    while (true)
    {
        bzero(buf, sizeof(buf));
        ssize_t bytes_read = read(sockfd, buf, sizeof(buf));
        if(bytes_read > 0)
        {
            printf("message from client fd %d: %s\n", sockfd, buf);
            write(sockfd, buf, sizeof(buf));
            close(sockfd);
        }
        else if(bytes_read == -1 && errno == EINTR)
        {
            printf("connect reading...");
            continue;
        }
        else if(bytes_read == -1 && ((errno == EAGAIN) || (errno == EWOULDBLOCK)))
        {
            printf("finish reading once, errno: %d\n", errno);
            break;
        }
        else if(bytes_read == 0)
        {
            printf("EOF, client fd %d disconnected\n", sockfd);
            close(sockfd);
            break;
        }
    }
    
}