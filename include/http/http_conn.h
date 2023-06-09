#ifndef HTTPCONNECTION_H_
#define HTTPCONNECTION_H_

#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <string>
#include <memory>
#include <stdarg.h>
#include <stdlib.h>
#include <map>

#include "lock/locker.h"
#include "timer/lst_timer.h"
#include "utils/util.h"
// #include "lru_cache.h"
#include "log/log.h"
#include "mysql/mysql.h"
#include "pool/sql_connection_pool.h"

#define MAX_CACHE_KEY_SIZE 200
#define MAX_CACHE_OBJ_SIZE 102400

class http_conn
{
public:
    /* 文件名的最大长度 */
    static const int FILENAME_LEN = 200;
    /* 读缓冲区的大小 */
    static const int READ_BUFFER_SIZE = 2048;
    /* 写缓冲区的大小 */
    static const int WRITE_BUFFER_SIZE = 1024;
    /* HTTP请求方法，目前仅支持GET */
    enum
    METHOD{GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH};
    /* 解析客户请求时，主状态机所处的状态 */
    enum
    CHECK_STATE{CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT};
    /* 服务器处理HTTP请求的可能结果 */
    enum
    HTTP_CODE{NO_REQUEST = 0, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST,
              FILE_REQUEST, CACHE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION};
    /* 行的读取状态 */
    enum LINE_STATUS{LINE_OK = 0, LINE_BAD, LINE_OPEN};

public:
    http_conn() {}
    ~http_conn() {}

public:
    /* 初始化新接受的连接 */
    void init(int sockfd, const sockaddr_in& addr);
    /* 从数据库中取出所有用户信息 */
    void initmysql_result(connection_pool* connPool);
    /* 关闭连接 */
    void close_conn(bool real_close = true);
    /* 处理客户请求 */
    void process();
    /* 非阻塞读操作 */
    bool read();
    /* 非阻塞写操作 */
    bool write();

private:
    /* 初始化连接 */
    void init();
    /* 解析HTTP请求 */
    HTTP_CODE process_read();
    /* 填充HTTP应答 */
    bool process_write(HTTP_CODE ret);
    /* 下面这一组函数被process_read()调用以分析HTTP请求 */
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_headers(char* text);
    HTTP_CODE parse_content(char* text);
    HTTP_CODE do_request();
    char* get_line() {return m_read_buf + m_start_line;}
    LINE_STATUS parse_line();

    /* 下面一组函数被process_write()调用以填充HTTP应答 */
    void unmap();
    bool add_response(const char* format, ...);
    bool add_content(const char* content);
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    /* 所有socket上的事件都被注册到同一个epoll内核事件表中，
        所以将epoll文件描述符设置为静态 */
    static int m_epollfd;
    /* 统计用户数量 */
    static int m_user_count;
    /* 服务器的全局静态缓存 */
    // std::shared_ptr<LRUCache> m_cache;
    /* 每个连接的定时器 */
    util_timer<http_conn>* m_timer;
    MYSQL* m_mysql;
private:
    /* 该HTTP连接的socket和对方的地址 */
    int m_sockfd;
    sockaddr_in m_address;
    /* 读缓冲区 */
    char m_read_buf[READ_BUFFER_SIZE];
    /* 标记读缓冲已经读入的客户数据的最后一个字节的下一个位置 */
    int m_read_idx;
    /* 当前正在分析的字符在缓冲区中的位置 */
    int m_checked_idx;
    /* 当前正在解析的行的初始位置 */
    int m_start_line;
    /* 写缓冲区 */
    char m_write_buf[WRITE_BUFFER_SIZE];
    /* 写缓冲区中待发送的字节数 */
    int m_write_idx;
    /* 需要发送的总字节数，包括文件中字节数 */
    long bytes_to_send;
    /* 需要发送的总字节数，包括文件中字节数 */
    long bytes_have_sent;
    /* 主状态机当前所处的状态 */
    CHECK_STATE m_check_state;
    /* 请求方法 */
    METHOD m_method;
    /* 客户请求的目标文件的完整路径 */
    char m_real_file[FILENAME_LEN];
    /* 客户请求的目标文件的文件名 */
    char* m_url;
    /* HTTP协议版本号，目前仅支持HTTP/1.1 */
    char* m_version;
    /* 主机名 */
    char* m_host;
    /* HTTP请求的消息体长度 */
    int m_content_length;
    /* HTTP请求是否要保持连接 */
    bool m_linger;
    /* 客户请求的目标文件被mmap到内存中的起始位置 */
    char* m_file_address;
    /* 目标文件的状态，通过它可以判断文件是否存在、是否是目录，是否可读、文件大小等信息 */
    struct stat m_file_stat;
    /* 采用writev执行写操作，m_iv_count表示被写内存块的数量 */
    struct iovec m_iv[2];
    int m_iv_count;

    /* 是否是POST请求 */
    int m_cgi;
    /* 请求消息体数据 */
    char* m_req_body;

    /* 用户密码map */
    // std::map<std::string, std::string> m_users;
    // int m_close_log;

    // char m_sql_user[100];
    // char m_sql_passwd[100];
    // char m_sql_name[100];
};

#endif
