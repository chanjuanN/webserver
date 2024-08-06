#include "sqlconnpool.h"

//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL* ConnectionPool::GetConnection() {
    MYSQL* con = nullptr;
    if(0==m_connList.size()) {
        return nullptr;
    }
    m_reserve.wait();//使用信号量的话可以唤醒阻塞等待链接的线程，否则就要轮询了
    lock.lock();
    con = m_connList.front();
    m_connList.pop_front();
    --m_freeConn;
    ++m_curConn;
    lock.unlock();

    return con;
}

// 释放当前使用的连接, 放回连接池中
bool ConnectionPool::ReleaseConnection(MYSQL* conn) {
    if(nullptr==conn) {
        return false;
    }

    lock.lock();
    m_connList.push_back(conn);
    ++m_freeConn;
    --m_curConn;
    lock.unlock();

    m_reserve.post();//唤醒阻塞的线程

    return true;
}


int ConnectionPool::GetFreeConn() {
    return this->m_freeConn;
}

void ConnectionPool::DestroyPool() {
    lock.lock();
    if(m_connList.size()>0) { //取出的连接是在线程中自己关闭吗
        list<MYSQL*>::iterator it;
        for(it=m_connList.begin(); it!=m_connList.end(); it++) {
            MYSQL* conn = *it;
            mysql_close(conn);//关闭连接，退出登陆
        }

        m_curConn = 0;
        m_freeConn = 0;
        m_connList.clear();
    }
    lock.unlock();
}
//单例模式
ConnectionPool* ConnectionPool::GetInstance() {
    static ConnectionPool m_connPool;
    return &m_connPool;
}

void ConnectionPool::init(string url, string user, string password, string databaseName, int port, int maxConn, int closeLog) {
    m_url = url;
    m_port = port;
    m_user = user;
    m_password = password;
    m_databaseName = databaseName;
    m_closeLog = closeLog;

    for(int i=0; i<maxConn; i++) { 
        MYSQL* con = nullptr;
        con = mysql_init(con);//初始化连接
        if(con==nullptr) {
            LOG_ERROR("%s", "Mysql error");
            exit(1);
        }
		//建立一个到mysql数据库的连接（登陆了）
        con = mysql_real_connect(con, m_url.c_str(), m_user.c_str(), m_password.c_str(), m_databaseName.c_str(), port, nullptr, 0);
        if(con==nullptr) {
            LOG_ERROR("%s", "Mysql error"); 
            exit(1);
        }

        m_connList.push_back(con);//放入连接池中
        ++m_freeConn;
    }
    // printf("空闲连接数：%d", m_freeConn);
    m_reserve = Sem(m_freeConn);//多少个连接就有多少个信号量
    m_maxConn = m_freeConn;
}

ConnectionPool::ConnectionPool() {
    m_curConn = 0;
    m_freeConn =0;
}
ConnectionPool::~ConnectionPool() {
    DestroyPool();
}


ConnectionRAII::ConnectionRAII(MYSQL** conn, ConnectionPool* connPool) {
    *conn = connPool->GetConnection();
    connRAII = *conn;
    poolRAII = connPool;
}

ConnectionRAII::~ConnectionRAII() {
    poolRAII->ReleaseConnection(connRAII);
}