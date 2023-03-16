#include "global_definition/global_definition.h"
#include "webserver/webserver.h"

sort_timer_lst<http_conn> WebServer::m_timer_list = sort_timer_lst<http_conn>();
int WebServer::m_pipefd[2] = {0, 0};

WebServer::WebServer()
{
    m_users = new http_conn[MAX_FD];

    /* 服务器资源文件夹 */
    char server_path[200];
    getcwd(server_path, 200);
    char root[17] = "/config/www/html";
    m_root = (char*)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    // m_cache = std::make_shared<LRUCache>(LRUCache(MAX_CACHE_LINE));

}

WebServer::~WebServer()
{
    delete m_root;
    delete m_serv_sock;
    delete m_serv_addr;
    delete m_ep;
    delete[] m_users;
    delete m_tpool;
    close(m_pipefd[0]);
    close(m_pipefd[1]);
    LOG_INFO("----------------Web Server ended----------------");
}

void WebServer::run(const YAML::Node& config_node)
{
    init(
        config_node["socket_port"].as<int>(),
        config_node["database_url"].as<std::string>(),
        config_node["database_user"].as<std::string>(),
        config_node["database_password"].as<std::string>(),
        config_node["database_port"].as<int>(),
        config_node["database_name"].as<std::string>(),
        config_node["max_sql_conntions"].as<int>(),
        config_node["max_thread_nums"].as<int>(),
        config_node["close_log"].as<int>()
    );
    init_log();
    LOG_INFO("----------------Web Server Start!----------------");
    LOG_INFO("Log Start Success!");
    printf("Log Start Success!\n");
    init_thread_pool();
    LOG_INFO("Thread Pool Start Success!");
    printf("Thread Pool Start Success!\n");
    init_sql_pool();
    LOG_INFO("MySQL Connection Pool Start Success!");
    printf("MySQL Connection Pool Start Success!\n");
    event_listen();
    event_loop();
}

void WebServer::init(int socket_port, std::string db_url, std::string db_user, std::string db_passWord, int db_port, std::string databaseName, int sql_num,
              int thread_num, int close_log)
{
    m_port = socket_port;
    m_dburl = db_url;
    m_dbuser = db_user;
    m_dbpasswd = db_passWord;
    m_dbport = db_port;
    m_dbName = databaseName;
    m_dbmaxConn = sql_num;
    m_thread_number = thread_num;
    m_close_log = close_log;
    m_stop_server = false;
}

void WebServer::init_thread_pool()
{
    m_tpool = new threadpool<http_conn>(m_thread_number);
}

void WebServer::init_sql_pool()
{
    m_dpool = connection_pool::GetInstance();
    m_dpool->init(m_dburl, m_dbuser, m_dbpasswd, m_dbName, m_dbport, m_dbmaxConn, m_close_log);
}

void WebServer::init_log()
{
    if(!m_close_log)
    {
        std::string log_file_name = WORK_SPACE_PATH + "/logs/";
        Log::get_instance()->init(log_file_name.c_str(), m_close_log, m_log_buf_size, m_log_split_lines, m_log_max_queue_size);

    }
}

void WebServer::event_listen()
{
    m_serv_sock = new Socket();

    /* 优雅关闭连接 */
    struct linger tmp = {1, 1};
    setsockopt(m_serv_sock->getFd(), SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    m_serv_addr = new InetAddress("127.0.0.1", m_port);
    m_serv_addr->addr.sin_addr.s_addr = htonl(INADDR_ANY);

    /* 复用ip */
    int flag = 1;
    setsockopt(m_serv_sock->getFd(), SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    m_serv_sock->bind(m_serv_addr);
    m_serv_sock->listen();

    m_ep = new Epoll();
    m_ep->addFd(m_serv_sock->getFd(), EPOLLIN | EPOLLET | EPOLLRDHUP);

    errif(socketpair( PF_UNIX, SOCK_STREAM, 0, m_pipefd) == -1, "socketpair error");
    m_ep->addFd(m_pipefd[0], EPOLLIN | EPOLLET);

    setnonblocking(m_pipefd[0]);
    setnonblocking(m_pipefd[1]);/* 减少信号处理函数处理时间，即使信号被忽略了对于该场景也无所谓 */

    Utils::u_pipefd = m_pipefd;

    addsig(SIGPIPE, SIG_IGN);
    addsig(SIGALRM, m_utils.sig_handler);
    addsig(SIGTERM, m_utils.sig_handler);

    http_conn::m_epollfd = m_ep->getFd();
    
    alarm(TIMESLOT);
}

void WebServer::event_loop()
{
    bool timeout = false;
    auto cb_func = [](http_conn* user_data ) {
            user_data->close_conn();
        };
    while(!m_stop_server)
    {
        // std::vector<epoll_event> events = m_ep->poll();
        // int number = events.size();
        int number = epoll_wait(m_ep->epfd, m_ep->events, MAX_EVENT_NUMBER, -1);
        if ((number < 0) && (errno != EINTR))
        {
            LOG_ERROR( "epoll failure" );
            break;
        }
        for(int i = 0; i < number; ++i)
        {
            int sockfd = m_ep->events[i].data.fd;
            if(sockfd == m_serv_sock->getFd())
            {
                process_client_data();
            }
            else if(sockfd == m_pipefd[0] && m_ep->events[i].events & EPOLLIN)
            {
                process_signal(timeout);
            }
            else if(m_ep->events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                /* 如果有异常，直接关闭客户连接 */
                LOG_ERROR("Event Error close %d" , sockfd);
                util_timer<http_conn>* timer = m_users[sockfd].m_timer;
                process_tiemr(timer, sockfd);
            }
            else if(m_ep->events[i].events & EPOLLIN)
            {
                process_read(sockfd);
            }
            else if(m_ep->events[i].events & EPOLLOUT)
            {
                process_write(sockfd);
            }
        }
        if(timeout)
        {
            m_timer_list.tick();
            alarm(TIMESLOT);
            timeout = false;
        }
    }
}

void WebServer::process_signal(bool& timeout)
{
    int sig;
    char signals[1024];
    int ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if(ret == -1 || ret == 0)
    {
        return;
    }
    else
    {
        for(int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM:
                // LOG_INFO("SIGALRM reach here");
                LOG_INFO("Timed out");
                timeout = true;
                    break;
                case SIGTERM:
                    m_stop_server = true;
                    break; 
                default:
                    break;
            }
        }
    }
}

void WebServer::process_client_data()
{
    struct sockaddr_in client_address;
    socklen_t client_addrlen = sizeof(client_address);
    int connfd = accept(m_serv_sock->getFd(), (struct sockaddr*)&client_address, &client_addrlen);
    if(connfd < 0)
    {
        LOG_ERROR("accept error, errno is %d\n", errno);
        return;
    }
    if(http_conn::m_user_count >= MAX_FD)
    {
        LOG_ERROR("Internal server busy");
        show_error(connfd, "Internal server busy");
        return;
    }
    /* 初始化客户连接 */
    m_users[connfd].init(connfd, client_address);
    // m_users[connfd].m_cache = m_cache;
    /* 初始化该连接的定时器 */
    time_t cur = time(NULL);
    auto cb_func = [](http_conn* user_data ) {
            user_data->close_conn();
        };
    util_timer<http_conn>* timer = new util_timer<http_conn>(cur + 3 * TIMESLOT, cb_func, &m_users[connfd]);
    m_users[connfd].m_timer = timer;
    m_timer_list.add_timer(timer);
    /* 初始化该连接的数据库 */
    m_users[connfd].initmysql_result(m_dpool);
}

void WebServer::process_tiemr(util_timer<http_conn>* timer, int sockfd)
{
    auto cb_func = [](http_conn* user_data ) {
            user_data->close_conn();
        };
    cb_func(m_users + sockfd);
    if(timer)
    {
        m_timer_list.del_timer(timer);
    }
    LOG_INFO( "close fd %d\n", sockfd );
}

void WebServer::process_read(int sockfd)
{
    /* proactor模式 */
    /* 根据读的结果，决定是将任务添加到线程池还是关闭连接 */
    util_timer<http_conn>* timer = m_users[sockfd].m_timer;
    if(m_users[sockfd].read())
    {
        /* 唤醒一个线程处理http_conn中读到的数据 */
        m_tpool->append(m_users + sockfd);
    }
    else
    {
        process_tiemr(timer, sockfd);
    }
}

void WebServer::process_write(int sockfd)
{
    /* proactor模式 */
    /* 根据写的结果，决定是否关闭连接 */
    util_timer<http_conn>* timer = m_users[sockfd].m_timer;
    if(m_users[sockfd].write())
    {
        LOG_INFO("Response Success!");
        if(timer)
        {
            time_t cur = time(NULL);
            timer->expire = cur + 3 * TIMESLOT;
            LOG_INFO("Adjust timer once");
            m_timer_list.adjust_timer(timer);
        }
        
    }
    else
    {
        process_tiemr(timer, sockfd);
    }
}