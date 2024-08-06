/* 
校验 & 数据库连接池
===============
数据库连接池
> * 单例模式，保证唯一
> * list实现连接池
> * 连接池为静态大小
> * 互斥锁实现线程安全

校验  
> * HTTP请求采用POST方式
> * 登录用户名和密码校验
> * 用户注册及多线程注册安全 */

/* 我们首先看单个数据库连接是如何生成的：
以下都是mysql库提供的方法
使用mysql_init()初始化连接
使用mysql_real_connect()建立一个到mysql数据库的连接
使用mysql_query()执行查询语句
使用result = mysql_store_result(mysql)获取结果集
使用mysql_num_fields(result)获取查询的列数，mysql_num_rows(result)获取结果集的行数
通过mysql_fetch_row(result)不断获取下一行，然后循环输出
使用mysql_free_result(result)释放结果集所占内存
使用mysql_close(conn)关闭连接 */

#ifndef SQLCONNPOOL_H
#define SQLCONNPOOL_H

#include <mysql/mysql.h>
#include <string>
#include "locker.h"
#include <list>
#include <stdlib.h>
#include "log.h"

using namespace std;

class ConnectionPool {
public:
    MYSQL* GetConnection();//获取一个数据库连接
    bool ReleaseConnection(MYSQL* conn);//释放一个连接
    int GetFreeConn();//获取空闲连接数
    void DestroyPool(); //销毁连接池

    //单例模式
    static ConnectionPool* GetInstance();

    void init(string url, string user, string password, string databaseName, int port, int maxConn, int closeLog);

private:
    ConnectionPool();
    ~ConnectionPool();

    int m_maxConn;//最大连接数
    int m_curConn;//当前已使用连接数
    int m_freeConn;//当前空闲连接数
    Locker lock; //多线程会访问连接池，需要锁保护共享资源

    list<MYSQL*> m_connList;//连接池
    Sem m_reserve;//有多少个空闲连接，信号量就等于多少

public:
    string m_url;//主机地址
    string m_port;//数据库端口号
    string m_user;//登陆数据库用户名
    string m_password;//登陆密码
    string m_databaseName;//使用的数据库名称
    int m_closeLog;//日志开关 
};


/* RAII机制是Resource Acquisition Is Initialization（wiki上面翻译成 “资源获取就是初始化”）的简称，是C++语言的一种管理资源、避免泄漏的惯用法。利用的就是C++构造的对象最终会被销毁的原则。
RAII的做法是使用一个对象，在其构造时获取对应的资源，在对象生命期内控制对资源的访问，使之始终保持有效，最后在对象析构的时候，释放构造时获取的资源。 */
//连接池的RAII类:自动管理资源,包含申请和释放
class ConnectionRAII {
public:
    ConnectionRAII(MYSQL** conn, ConnectionPool* connPool);
    ~ConnectionRAII();

private:
    MYSQL* connRAII;
    ConnectionPool* poolRAII;
};

#endif