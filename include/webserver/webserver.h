#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <assert.h>
#include <string>
#include <vector>
#include <memory>
#include <yaml-cpp/yaml.h>

#include "pool/threadpool.h"
#include "http/http_conn.h"
#include "utils/Epoll.h"
#include "utils/Socket.h"
#include "utils/InetAddress.h"
#include "timer/lst_timer.h"
#include "utils/util.h"
#include "utils/lru_cache.h"
#include "pool/sql_connection_pool.h"
#include "log/log.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000
#define MAX_CACHE_KEY_SIZE 200
#define MAX_CACHE_OBJ_SIZE 102400
#define MAX_CACHE_LINE 1024
#define TIMESLOT 20

class WebServer
{
public:
    WebServer();
    ~WebServer();

    void init(int socket_port, std::string db_url, std::string db_user, std::string db_passWord, int db_port, std::string databaseName, int sql_num,
              int thread_num, int close_log);
    
    void init_thread_pool();
    void init_sql_pool();
    void init_log();
    void event_listen();
    void event_loop();
    void process_client_data();
    void process_tiemr(util_timer<http_conn>* timer, int sockfd);
    void process_signal(bool& timeout);
    void process_read(int sockfd);
    void process_write(int soclfd);

    void run(const YAML::Node& config_node);

public:
    /* 网站主目录 */
    char* m_root;

    /* Socket连接 */
    int m_port;
    Socket* m_serv_sock;
    InetAddress* m_serv_addr;

    /* epoll */
    Epoll* m_ep;

    /* 定时器容器 */
    static sort_timer_lst<http_conn> m_timer_list;

    /* 信号通知管道，用于统一事件 */
    static int m_pipefd[2];

    /* 用户连接数组 */
    http_conn* m_users;

    /* 用户连接数 */
    int m_user_count;
    
    /* 启停服务器 */
    bool m_stop_server;

    /* 服务器缓存 */
    // std::shared_ptr<LRUCache> m_cache;

    /* 线程池 */
    threadpool<http_conn>* m_tpool;
    int m_thread_number;

    /* 数据库连接池 */
    connection_pool* m_dpool;
    std::string m_dburl;
    std::string m_dbuser;
    std::string m_dbpasswd;
    std::string m_dbName;
    int m_dbport;
    int m_dbmaxConn;

    /* 日志 */
    int m_close_log;
    int m_log_buf_size;
    int m_log_split_lines;
    int m_log_max_queue_size;

    /* 一些通用函数 */
    Utils m_utils;
};

#endif