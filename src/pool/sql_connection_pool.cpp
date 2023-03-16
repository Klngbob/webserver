#include "pool/sql_connection_pool.h"

connection_pool::connection_pool()
{
    m_CurConn = 0;
    m_FreeConn = 0;
}

connection_pool* connection_pool::GetInstance()
{
    static connection_pool connPool;
    return &connPool;
}

void connection_pool::init(std::string url, std::string user, std::string passwd, std::string databaseName,
                int port, int maxConn, int close_log)
{
    m_Url = url;
    m_User = user;
    m_PassWord = passwd;
    m_DataBaseName = databaseName;
    m_Port = port;
    m_MaxConn = maxConn;
    m_close_log = close_log;

    for(int i = 0; i < maxConn; ++i)
    {
        MYSQL* conn = NULL;
        conn = mysql_init(conn);

        if(conn == NULL)
        {
            LOG_ERROR("MySQL Error");
            exit(1);
        }
        conn = mysql_real_connect(conn, url.c_str(), user.c_str(), passwd.c_str(), databaseName.c_str(),
                                    port, NULL, 0);
        if(conn == NULL)
        {
            LOG_ERROR("MySQL Error");
            exit(1);
        }
        connList.push_back(conn);
        m_FreeConn++;
    }

    reserve = sem(m_FreeConn);
    m_MaxConn = m_FreeConn;
}

/* 当有数据库连接请求时，从连接池内取出一个可用连接 */
MYSQL* connection_pool::GetConnection()
{
    MYSQL* conn = NULL;
    if(!connList.size())
        return NULL;
    
    reserve.wait();
    m_mutex.lock();

    conn = connList.front();
    connList.pop_front();
    m_FreeConn--;
    m_CurConn++;

    m_mutex.unlock();
    return conn;
}

/* 释放当前连接 */
bool connection_pool::ReleaseConnection(MYSQL* conn)
{
    if(conn == NULL)
        return false;
    
    m_mutex.lock();

    connList.push_back(conn);
    ++m_FreeConn;
    --m_CurConn;

    m_mutex.unlock();

    reserve.post();
    return true;
}

/* 销毁数据库连接池 */
void connection_pool::DestroyPool()
{
    m_mutex.lock();
    if(connList.size())
    {
        for(auto conn : connList)
        {
            mysql_close(conn);
        }
        m_CurConn = 0;
        m_FreeConn = 0;
        connList.clear();
    }

    m_mutex.unlock();
}

int connection_pool::GetFreeConn()
{
    return m_FreeConn;
}

connection_pool::~connection_pool()
{
    DestroyPool();
}

connectionRAII::connectionRAII(MYSQL** mysql, connection_pool* pool)
{
    *mysql = pool->GetConnection();

    connRAII = *mysql;
    poolRAII = pool;
}

connectionRAII::~connectionRAII()
{
    poolRAII->ReleaseConnection(connRAII);
}