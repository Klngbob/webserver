#ifndef SQL_CONNECTION_POOL_H
#define SQL_CONNECTION_POOL_H

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <string>
#include "lock/locker.h"
#include "log/log.h"

class connection_pool
{
public:
    MYSQL* GetConnection();                 /* 获取数据库连接 */
    bool ReleaseConnection(MYSQL* conn);    /* 释放连接 */
    int GetFreeConn();                      /* 获取连接池中空闲连接数 */
    void DestroyPool();                     /* 销毁所有连接 */

    static connection_pool* GetInstance();

    void init(std::string url, std::string user, std::string passwd, std::string databaseName,
                int port, int maxConn, int close_log);

private:
    connection_pool();
    ~connection_pool();

public:
    std::string m_Url;  /* 主机地址 */
    std::string m_Port; /* 数据库端口号 */
    std::string m_User; /* 登录数据库用户名 */
    std::string m_PassWord; /* 登录数据库密码 */
    std::string m_DataBaseName; /* 数据库名 */
    int m_close_log;    /* 日志开关 */

private:
    int m_MaxConn;  /* 最大连接数 */
    int m_CurConn;  /* 当前使用连接数 */
    int m_FreeConn; /* 当前空闲连接数 */
    locker m_mutex;
    std::list<MYSQL*> connList; /* 连接池容器 */
    sem reserve;
};

class connectionRAII
{
public:
    connectionRAII(MYSQL** connRAII, connection_pool* poolRAII);
    ~connectionRAII();

private:
    MYSQL* connRAII;
    connection_pool* poolRAII;
};

#endif