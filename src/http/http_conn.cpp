#include "http/http_conn.h"
#include "lock/locker.h"
#include "utils/util.h"
#include "global_definition/global_definition.h"

#include <fstream>
#include <sys/wait.h>

/* 定义HTTP相应的一些状态信息 */
const char* ok_200_title = "OK";
const char* error_400_titile = "Bad Request";
const char* error_400_form = "Your request has had syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";

const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

/* 网站根目录 */
std::string d_root = WORK_SPACE_PATH + "/www/html";
const char* doc_root = d_root.c_str();

locker m_mutex;
std::map<std::string, std::string> m_users;

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

void http_conn::close_conn(bool real_close)
{
    if(real_close && (m_sockfd != -1))
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--; /* 关闭一个连接则客户总数少一 */
    }
}

void http_conn::init(int sockfd, const sockaddr_in& addr)
{
    m_sockfd = sockfd;
    m_address = addr;
    /* 下面两行避免TIME_WAIT状态，用于调试 */
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    addfd(m_epollfd, sockfd, true);
    m_user_count++;

    m_timer = nullptr;
    m_mysql = NULL;
    // m_close_log = close_log;
    // strcpy(m_sql_user, user.c_str());
    // strcpy(m_sql_passwd, passwd.c_str());
    // strcpy(m_sql_name, name.c_str());

    init();
}

void http_conn::init()
{
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx =0;
    m_write_idx = 0;

    bytes_to_send = 0;
    bytes_have_sent = 0;
    // m_mysql = NULL;
    m_cgi = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

void http_conn::initmysql_result(connection_pool* connPool)
{
    std::ofstream out(WORK_SPACE_PATH + "/config/id_passwd.txt");
    /* 从数据库连接池中取出一个连接 */
    connectionRAII mysqlConn(&m_mysql, connPool);

    if(mysql_query(m_mysql, "SELECT user,passwd FROM users"))
    {
        LOG_ERROR("SELECT Error: %s\n", mysql_error(m_mysql));
    }

    /* 从表中检索完整的结果集 */
    MYSQL_RES* result = mysql_store_result(m_mysql);

    /* 返回结果集中的列数 */
    int num_fields = mysql_num_fields(result);

    /* 返回所有字段结构的数组 */
    MYSQL_FIELD* fields = mysql_fetch_fields(result);

    /* 将按行取出的用户密码存入map中 */
    while(MYSQL_ROW row = mysql_fetch_row(result))
    {
        std::string tmp1(row[0]);
        std::string tmp2(row[1]);
        out << tmp1 << " " << tmp2 << std::endl;
        m_users[tmp1] = tmp2;
    }
    mysql_free_result(result);
    out.close();
}

http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for(; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        temp = m_read_buf[m_checked_idx];
        if(temp == '\r')
        {
            /* 如果'\r'是最后一个字符，则这次分析没有读取到一个完整的行，返回LINE_OPEN
               表示需要继续读取客户数据才能进一步分析 */
            if((m_checked_idx + 1) == m_read_idx)
            {
                return LINE_OPEN;
            }
            else if(m_read_buf[m_checked_idx + 1] == '\n') /* 请求行以\r\n结尾 */
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            /* 否则存在语法问题 */
            return LINE_BAD;
        }
        else if(temp == '\n')
        {
            if((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r'))
            {
                m_read_buf[m_checked_idx-1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    /* 如果读完了还没读到'\r'则说明还有数据需要读 */
    return LINE_OPEN;
}

/* 循环读取客户数据，直到无数据可读或者对方关闭连接 */
bool http_conn::read()
{
    if(m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }
    int bytes_read = 0;
    while (true)
    {
        bytes_read = recv(m_sockfd, m_read_idx + m_read_buf, READ_BUFFER_SIZE - m_read_idx, 0);
        if(bytes_read == -1)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            return false;
        }
        else if(bytes_read == 0)
        {
            return false;
        }
        m_read_idx += bytes_read;
    }
    return true;
}

/* 解析HTTP请求行，获得请求方法、目标URL和HTTP版本号 */
http_conn::HTTP_CODE http_conn::parse_request_line(char* text)
{
    m_url = strpbrk(text, " \t"); /* 找到第一个匹配'\t'的下标 */
    if(!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    char* method = text;
    if(strcasecmp(method, "GET") == 0)
    {
        m_method = GET;
    }
    else if(strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        m_cgi = 1;
    }
    else
    {
        return BAD_REQUEST;
    }
    m_url += strspn(m_url, " \t"); /* 找到url的起始位置，也就是去除url前的\t */
    m_version = strpbrk(m_url, " \t"); /* 指向url后一个\t */
    if(!m_version)
    {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if(strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }
    if(strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if(strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    if(!m_url || m_url[0] != '/')
    {
        return BAD_REQUEST;
    }
    /* 如果url结尾是'/'，则返回默认界面 */
    if(strlen(m_url) == 1)
    {
        strcat(m_url, "index.html");
    }
    m_check_state = CHECK_STATE_HEADER; /* 状态转移至检查请求头 */
    return NO_REQUEST;
}

/* 解析HTTP请求的一个头部信息 */
http_conn::HTTP_CODE http_conn::parse_headers(char* text)
{
    /* 遇到空行，表示头部字段解析完毕 */
    if(text[0] == '\0')
    {
        /* 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
            状态机转移到CHECK_STATE_CONTENT */
        if(m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    /* 处理Connection头部字段 */
    else if(strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive") == 0)
        {
            // printf("text = %s\n", text);
            m_linger = true;
        }
    }
    /* 处理Content-Length头部字段 */
    else if(strncasecmp(text, "Content-Length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if(strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        LOG_INFO("Oop! Unknown header %s\n", text);
    }
    return NO_REQUEST;
}

/* 解析HTTP请求消息体，只检查是否读取到 */
http_conn::HTTP_CODE http_conn::parse_content(char* text)
{
    if(m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        m_req_body = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

/* 分析HTTP请求的主状态机 */
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
    while (((m_check_state == CHECK_STATE_CONTENT) &&
            (line_status == LINE_OK)) || ((line_status = parse_line()) == LINE_OK))
    {
        text = get_line();
        m_start_line = m_checked_idx; /* 记录下一行位置 */
        LOG_INFO("Got 1 http line: %s", text);
        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parse_request_line(text);
            if(ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            break;
        }
        case CHECK_STATE_HEADER:
        {
            ret = parse_headers(text);
            if(ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            if(ret == GET_REQUEST)
            {
                return do_request();
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            ret = parse_content(text);
            if(ret == GET_REQUEST)
            {
                return do_request();
            }
            line_status = LINE_OPEN;
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

/* 当得到一个完整、正确的HTTP请求时，先分析目标文件的属性，如果目标文件
    存在、对所有用户可读，且不是目录，则使用mmap将其映射到内存地址m_file_address处，
    并告诉调用者获取文件成功 */
http_conn::HTTP_CODE http_conn::do_request()
{
    // printf("get request!\n");
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    const char* p = strrchr(m_url, '/');
    // LOG_INFO("Do request: url = %s", m_url);
    // printf("Do request: url = %s\n", m_url);

    /* 处理cgi，登录和注册校验 */
    if(m_cgi && (*(p + 1) == '2' || *(p + 1) == '3'))
    {
        char flag = m_url[1];
        // LOG_INFO("verify flag is %c", flag);

        char* url_real = (char*)malloc(sizeof(char) * FILENAME_LEN);
        strcpy(url_real, "/");
        strcat(url_real, m_url + 2);
        strncpy(m_real_file + len, url_real, FILENAME_LEN - len - 1);
        free(url_real);
        // LOG_INFO("process CGI, m_real_file is %s", m_real_file);
        /* 从请求体消息中取出用户名和密码user=kingbob&passwd=123 */
        char user[100], password[100];
        int i;
        for (i = 5; m_req_body[i] != '&'; ++i)
            user[i - 5] = m_req_body[i];
        user[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_req_body[i] != '\0'; ++i, ++j)
            password[j] = m_req_body[i];
        password[j] = '\0';

        /* 注册请求 */
        if(flag == '3')
        {
            char* sql_insert = (char*)malloc(sizeof(char) * FILENAME_LEN);
            strcpy(sql_insert, "INSERT INTO users(user,passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, user);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            /* 判断是否有重名 */
            if(m_users.find(user) == m_users.end())
            {
                m_mutex.lock();
                // printf("m_mysql is null? %d\n", m_mysql == NULL);
                int res = mysql_query(m_mysql, sql_insert);
                m_users.insert(std::pair<std::string, std::string>(user, password));
                m_mutex.unlock();

                if(!res)
                {
                    strcpy(m_url, "/login.html");
                    m_mutex.lock();
                    std::ofstream out(WORK_SPACE_PATH + "/config/id_passwd.txt", std::ios::app);
                    out << user << " " << password << std::endl;
                    out.close();
                    m_mutex.unlock();
                }
                else
                {
                    strcpy(m_url, "/registerError.html");
                }
            }
            else
            {
                strcpy(m_url, "/registerError.html");
            }

            // free(sql_insert);
        }
        /* 登录请求校验 */
        else if(flag == '2')
        {
            // LOG_INFO("开始登录校验, m_real_file = %s", m_real_file);
            pid_t pid;
            int pipefd[2];
            if(pipe(pipefd) < 0)
            {
                LOG_ERROR("pipe() error:%d", 4);
                return BAD_REQUEST;
            }
            if((pid = fork()) < 0)
            {
                LOG_ERROR("fork() error:%d", 3);
                return BAD_REQUEST;
            }

            if(pid == 0)
            {
                LOG_INFO("子进程开始执行cgi校验");
                /* 将标准输出重定向到写端 */
                dup2(pipefd[1], 1);
                /* 关掉管道的写端 */
                close(pipefd[0]);
                /* 子进程执行cgi进程，m_real_file, user, password作为参数输入*/
                std::string file_path = WORK_SPACE_PATH + "/config/id_passwd.txt";
                execl(m_real_file, user, password, file_path.c_str(), NULL);
            }
            else
            {
                LOG_INFO("父进程等待子进程结果");
                /* 父进程关闭写端，打开读端读取子进程的输出 */
                close(pipefd[1]);
                char result;
                int ret = ::read(pipefd[0], &result, 1);

                if(ret != 1)
                {
                    LOG_ERROR("管道read error: ret=%d", ret);
                    return BAD_REQUEST;
                }

                LOG_INFO("登录检测");
                if(result == '1')
                {
                    strcpy(m_url, "/welcome.html");
                }
                else
                {
                    strcpy(m_url, "/loginError.html");
                }

                /* 等待子进程退出 */
                waitpid(pid, NULL, 0);
            }
        }
    }
    // printf("p = %s\n", p);
    /* GET请求中的注册 */
    if(*(p + 1) == '0')
    {
        char* url_real = (char*)malloc(sizeof(char) * FILENAME_LEN);
        strcpy(url_real, "/register.html");
        strncpy(m_real_file + len, url_real, FILENAME_LEN - len - 1);
        free(url_real);
    }
    /* GET请求中的登录 */
    else if(*(p + 1) == '1')
    {
        char* url_real = (char*)malloc(sizeof(char) * FILENAME_LEN);
        strcpy(url_real, "/login.html");
        strncpy(m_real_file + len, url_real, FILENAME_LEN - len - 1);
        free(url_real);
    }
    else if(*(p + 1) == '5')
    {
        char* url_real = (char*)malloc(sizeof(char) * FILENAME_LEN);
        strcpy(url_real, "/picture.html");
        strncpy(m_real_file + len, url_real, FILENAME_LEN - len - 1);
        free(url_real);
    }
    else if(*(p + 1) == '6')
    {
        char* url_real = (char*)malloc(sizeof(char) * FILENAME_LEN);
        strcpy(url_real, "/video.html");
        strncpy(m_real_file + len, url_real, FILENAME_LEN - len - 1);
        free(url_real);
    }
    else if(*(p + 1) == '7')
    {
        char* url_real = (char*)malloc(sizeof(char) * FILENAME_LEN);
        strcpy(url_real, "/fans.html");
        strncpy(m_real_file + len, url_real, FILENAME_LEN - len - 1);
        free(url_real);
    }
    else{
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
        // printf("else m_real_file is %s\n", m_real_file);
    }
    
    // if(!strcmp(m_cache->get(m_real_file), "")) /* cache里没有该对象 */
    // {
        if(stat(m_real_file, &m_file_stat) < 0)
        {
            return NO_RESOURCE;
        }
        if(!(m_file_stat.st_mode & S_IROTH)) /* 没有其他组读权限 */
        {
            return FORBIDDEN_REQUEST;
        }
        if(S_ISDIR(m_file_stat.st_mode))
        {
            return BAD_REQUEST;
        }
        int fd = open(m_real_file, O_RDONLY);
        m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if(m_file_stat.st_size < MAX_CACHE_OBJ_SIZE) /* 最大缓存文件字节数 */
        {
            char tempfile[MAX_CACHE_OBJ_SIZE];
            memcpy(tempfile, m_file_address, m_file_stat.st_size);
            // printf("tempfile file is %d\n", sizeof(tempfile));
            // m_cache->put(m_real_file, tempfile, m_file_stat.st_size);
            // printf("cache file len is %d\n", m_file_stat.st_size);
        }
        close(fd);
        return FILE_REQUEST;
    // }
    // else
    // {
    //     LOG_INFO("Cache hit");
    //     m_file_address = (char*)m_cache->get(m_real_file);
    //     return CACHE_REQUEST;
    // }
}

/* 对内存映射区执行munmap操作 */
void http_conn::unmap()
{
    if(m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

/* 写HTTP响应 */
bool http_conn::write()
{
    int temp = 0;
    // int bytes_to_send = m_write_idx; /* 有bug */
    if(bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }
    while (1)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        // printf("Current sent %ld bytes\n", temp);
        // printf("bytes have sent: %ld bytes\n", bytes_have_sent);
        if(temp < 0)
        {
            /* 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，
                在此期间，服务器无法立即接收到同一个用户的下一个请求，
                但这可以保证连接的完整性 */
            if(errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_sent += temp;
        if(bytes_have_sent >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0; /* 不发送响应头了 */
            m_iv[1].iov_base = m_file_address + (bytes_have_sent - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else{
            m_iv[0].iov_base = m_write_buf + bytes_have_sent;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_sent;
        }
        if(bytes_to_send <= 0)
        {
            /* 发送HTTP响应成功，根据HTTP请求中的Connection字段
                决定是否立即关闭连接。 */
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);
            
            if(m_linger)
            {
                init();
                // printf("http_conn::write() reach here\n");
                return true;
            }
            else
            {
                return false;
            }
        }
    }
    
}

/* 往写缓冲中写入待发送的数据 */
bool http_conn::add_response(const char* format, ...)
{
    if(m_write_idx >= WRITE_BUFFER_SIZE)
    {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 
                        m_write_idx - 1, format, arg_list);
    if(len >= (WRITE_BUFFER_SIZE - m_write_idx - 1))
    {
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status, const char* title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_linger();
    add_blank_line();
}

bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n", (m_linger == true? "keep-alive": "close"));
}

bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char* content)
{
    return add_response("%s", content);
}

/* 根据服务器处理HTTP请求的结果，决定返回给客户端的内容 */
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
    case INTERNAL_ERROR:
        {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form))
            {
                return false;
            }
            break;
        }
    case BAD_REQUEST:
        {
            add_status_line(400, error_400_titile);
            add_headers(strlen(error_400_form));
            if(!add_content(error_400_form))
            {
                return false;
            }
            break;
        }
    case NO_RESOURCE:
        {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if(!add_content(error_404_form))
            {
                return false;
            }
            break;
        }
    case FORBIDDEN_REQUEST:
        {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form))
            {
                return false;
            }
            break;
        }
    case FILE_REQUEST:
        {   add_status_line(200, ok_200_title);
            if(m_file_stat.st_size != 0)
            {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true;
            }
            else
            {
                const char* ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if(!add_content(ok_string))
                {
                    return false;
                }
            }
            // break;
        }
    // case CACHE_REQUEST:
    //     {
    //         add_status_line(200, ok_200_title);
    //         add_headers(m_cache->getValSize(m_real_file));
    //         m_iv[0].iov_base = m_write_buf;
    //         m_iv[0].iov_len = m_write_idx;
    //         m_iv[1].iov_base = m_file_address;
    //         m_iv[1].iov_len = m_cache->getValSize(m_real_file);
    //         m_iv_count = 2;
    //         bytes_to_send = m_write_idx + m_iv[1].iov_len;
    //         // printf("cache len again is %d\n", m_iv[1].iov_len);
    //         // printf("cache request done!\n");
    //         return true;
    //         /* test for cache */
    //         // add_headers(cache->get(temp).size() + strlen("\n<p>This is from cache!</p>\n"));
    //         // if(!add_content(cache->get(m_real_file).c_str()))
    //         // {
    //         //     return false;
    //         // }
    //         // if(!add_content("\n<p>This is from cache!</p>\n"))
    //         // {
    //         //     return false;
    //         // }
    //     }
    default:
        return false;
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

/* 由线程池中的工作线程调用，这是处理HTTP请求的入口函数 */
void http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    bool write_ret = process_write(read_ret);
    if(!write_ret)
    {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}