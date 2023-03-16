#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "log/log.h"

Log::Log()
{
    m_count = 0;
    m_is_sync = false;
}

Log::~Log()
{
    if(m_fp != NULL)
    {
        fclose(m_fp);
    }
}

/* 异步需要设置阻塞队列的长度，同步不需要设置 */ 
bool Log::init(const char* file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size)
{
    /* 如果设置了max_queue_size，则设置为异步 */
    if(max_queue_size >= 1)
    {
        m_is_sync = true;
        m_log_queue = new block_queue<std::string>(max_queue_size);
        pthread_t tid;
        /* 创建线程异步写日志 */
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    m_close_log = close_log;
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    m_split_lines = split_lines;

    time_t t = time(NULL);
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    /* 找到file_name最后出现'/'的位置 */
    const char* p = strrchr(file_name, '/');
    char log_full_name[256] = {0};

    /* 自定义日志名 */
    if(p == NULL)
    {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else
    {
        strcpy(log_name, p + 1);
        strncpy(dir_name, file_name, p - file_name + 1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;

    m_fp = fopen(log_full_name, "a");
    if(m_fp == NULL)
    {
        return false;
    }
    return true;
}

void Log::write_log(int level, const char* format, ...)
{
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};
    switch (level)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warning]:");
        break;
    case 3:
        strcpy(s, "[error]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }

    m_mutex.lock();
    /* 更新现有行数 */
    m_count++;

    /* 日志不是今天 或者 写入的日志行数是最大行数的倍数 */
    if(m_today != my_tm.tm_mday || m_count % m_split_lines == 0)
    {
        char new_log[256] = {0};
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};

        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        /* 如果时间不是今天，则创建今天的日志 */
        if(m_today != my_tm.tm_mday)
        {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }
        /* 如果是超过了日志最大行则在原日志名上加后缀 */
        else
        {
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }

        m_fp = fopen(new_log, "a");
    }

    m_mutex.unlock();
    
    va_list valist;
    va_start(valist, format);

    std::string log_str;
    m_mutex.lock();
    /* 内容格式化，用于向字符串中打印数据、数据格式用户自定义，返回写入到字符数组str中的字符个数(不包含终止符) */
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valist);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;

    m_mutex.unlock();

    /* 如果是异步写日志则直接放入阻塞队列，否则直接写入日志文件 */
    if(m_is_sync && !m_log_queue->full())
    {
        m_log_queue->push(log_str);
    }
    else
    {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }
    va_end(valist);
}

void Log::flush(void)
{
    m_mutex.lock();
    /* 强制刷新写入流缓冲区 */
    fflush(m_fp);
    m_mutex.unlock();
}