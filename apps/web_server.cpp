#include <sys/socket.h>
#include <netinet/in.h>
#include <assert.h>
#include <string>
#include <vector>
#include <memory>
#include "pool/threadpool.h"
#include "http/http_conn.h"
#include "utils/Epoll.h"
#include "utils/Socket.h"
#include "utils/InetAddress.h"
#include "timer/lst_timer.h"
#include "utils/util.h"
#include "pool/sql_connection_pool.h"
#include "log/log.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000
#define MAX_CACHE_KEY_SIZE 200
#define MAX_CACHE_OBJ_SIZE 102400
#define MAX_CACHE_LINE 1024
#define TIMESLOT 100

static sort_timer_lst<http_conn> timer_list;
static int pipefd[2];

int setnonblocking( int fd )
{
    int old_option = fcntl( fd, F_GETFL );
    int new_option = old_option | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_option );
    return old_option;
}

void sig_handler( int sig )
{
    int save_errno = errno;
    int msg = sig;
    send( pipefd[1], ( char* )&msg, 1, 0 );
    errno = save_errno;
}

void addsig(int sig, void(handler)(int), bool restart = true)
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

void show_error(int connfd, const char* info)
{
    printf("Error: %s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

void timer_handler()
{
    timer_list.tick();
    alarm( TIMESLOT );
}

int main(int argc, char* argv[])
{
    if( argc <= 2 )
    {
        printf( "usage: %s ip_address port_number\n", basename( argv[0] ) );
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi( argv[2] );
    /* 忽略服务器主动关闭连接的信号 */
    addsig(SIGPIPE, SIG_IGN); 
    /* 创建线程池 */
    threadpool<http_conn> *pool = NULL;
    try
    {
        pool = new threadpool<http_conn>;
    }
    catch(...)
    {
        return 1;
    }
    
    /* 创建数据库连接池 */
    connection_pool* dpool = connection_pool::GetInstance();
    dpool->init("localhost", "root", "123", "webserver", 3306, 10, 1);
    Log::get_instance()->init("/home/kingbob/projects/linux-network-programming/codes/webserver/log/", 0, 2000, 800000, 800);

    /*  */
    http_conn* users = new http_conn[MAX_FD];
    assert(users);

    std::shared_ptr<LRUCache> cache(new LRUCache(MAX_CACHE_LINE));
    assert(cache);

    int user_count = 0;

    Socket* serv_sock = new Socket();

    struct linger tmp = {1, 0};
    setsockopt(serv_sock->getFd(), SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    InetAddress* serv_addr = new InetAddress(ip, port);

    serv_sock->bind(serv_addr);
    serv_sock->listen();

    Epoll* ep = new Epoll();
    ep->addFd(serv_sock->getFd(), EPOLLIN | EPOLLET | EPOLLRDHUP);
    /* 信号事件 */
    int ret = socketpair( PF_UNIX, SOCK_STREAM, 0, pipefd );
    assert( ret != -1 );
    ep->addFd(pipefd[0], EPOLLIN | EPOLLET);
    setnonblocking(pipefd[0]);
    setnonblocking(pipefd[1]);/* 减少信号处理函数处理时间，即使信号被忽略了对于该场景也无所谓 */

    addsig(SIGALRM, sig_handler);
    addsig(SIGTERM, sig_handler);
    
    http_conn::m_epollfd = ep->getFd();

    auto cb_func = [](http_conn* user_data ) {
            user_data->close_conn();
        };
    bool timeout = false;
    bool stop_server = false;
    alarm(TIMESLOT);
    LOG_INFO("----------------Web Server Start!----------------");
    while (!stop_server)
    {
        std::vector<epoll_event> events = ep->poll();
        int number = events.size();
        if ((number < 0) && (errno != EINTR))
        {
            LOG_ERROR( "epoll failure" );
            break;
        }
        for(int i = 0; i < number; ++i)
        {
            int sockfd = events[i].data.fd;
            if(sockfd == serv_sock->getFd())
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                int connfd = accept(sockfd, (struct sockaddr*)&client_address, &client_addrlen);
                if(connfd < 0)
                {
                    printf("accept error, errno is %d\n", errno);
                    continue;
                }
                if(http_conn::m_user_count >= MAX_FD)
                {
                    show_error(connfd, "Internal server busy");
                    continue;
                }
                /* 初始化客户连接 */
                users[connfd].init(connfd, client_address);
                users[connfd].m_cache = cache;
                /* 初始化该连接的定时器 */
                util_timer<http_conn>* timer = new util_timer<http_conn>;
                timer->cb_func = cb_func;
                timer->user_data = &users[connfd];
                time_t cur = time(NULL);
                timer->expire = cur + 3 * TIMESLOT;
                users[connfd].m_timer = timer;
                timer_list.add_timer(timer);
                /* 初始化该连接的数据库 */
                users[connfd].initmysql_result(dpool);
            }
            else if(sockfd == pipefd[0] && events[i].events & EPOLLIN)
            {
                int sig;
                char signals[1024];
                int ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if(ret == -1 || ret == 0)
                {
                    continue;
                }
                else
                {
                    for(int i = 0; i < ret; ++i)
                    {
                        switch (signals[i])
                        {
                        case SIGALRM:
                            LOG_INFO("SIGALRM reach here");
                            LOG_INFO("Timed out");
                            timeout = true;
                            break;
                        case SIGTERM:
                            stop_server = true;
                            break; 
                        default:
                            break;
                        }
                    }
                }
            }
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                /* 如果有异常，直接关闭客户连接 */
                LOG_ERROR("Event Error close %d.\n" , sockfd);
                util_timer<http_conn>* timer = users[sockfd].m_timer;
                cb_func(users + sockfd);
                if(timer)
                {
                    timer_list.del_timer(timer);
                }
            }
            else if(events[i].events & EPOLLIN)
            {
                /* 根据读的结果，决定是将任务添加到线程池还是关闭连接 */
                util_timer<http_conn>* timer = users[sockfd].m_timer;
                if(users[sockfd].read())
                {
                    pool->append(users + sockfd); /* 唤醒一个线程处理http_conn中读到的数据 */
                }
                else
                {
                    // printf("read Error close %d.\n", sockfd);
                    // users[sockfd].close_conn();
                    cb_func(users + sockfd);
                    if(timer)
                    {
                        timer_list.del_timer(timer);
                    }
                }
            }
            else if(events[i].events & EPOLLOUT)
            {
                /* 根据写的结果，决定是否关闭连接 */
                util_timer<http_conn>* timer = users[sockfd].m_timer;
                if(!users[sockfd].write())
                {
                    // printf("write done close %d.\n", sockfd);
                    // users[sockfd].close_conn(); /* 应使用定时器定期处理非活动连接而不是立即关闭 */
                    cb_func(users + sockfd);
                    if(timer)
                    {
                        timer_list.del_timer(timer);
                    }
                }
                else /* 写事件成功完成了之后应该? */
                {
                    if(timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        LOG_INFO("Adjust timer once");
                        timer_list.adjust_timer(timer);
                    }
                }
            }
        }
        if(timeout)
        {
            timer_handler();
            timeout = false;
        }
    }
    delete serv_sock;
    delete serv_addr;
    delete ep;
    delete[] users;
    delete pool;
    close(pipefd[0]);
    close(pipefd[1]);

    LOG_INFO("----------------Web Server ended----------------");
    return 0;
}