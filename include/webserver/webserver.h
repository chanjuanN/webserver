#ifndef WEBSERVER_H
#define WEBSERVER_H


#include <sys/socket.h>
#include "locker.h"
#include "httpconn.h"
#include "sqlconnpool.h"
#include "threadpool.h"
#include "lst_timer.h"
#include <string>
#include <arpa/inet.h>

using namespace std;

const int MAX_FD = 65536;
const int MAX_EVENT_NUMBER = 10000;
const int TIMESLOT = 5;

class WebServer {
public:
    WebServer();
    ~WebServer();

    void init(int port, string user, string password, string databaseName, int asyncLogWrite, int optLinger, 
            int trigMode, int sqlNum, int threadNum, int closeLog, int concurrencyModel);
    void threadPool();
    void sqlPool();
    void logWrite();
    void trigMode();//lt/et
    void eventListen();
    void eventLoop();
    void timer(int connfd, struct sockaddr_in clientAddr);
    void adjustTimer(UtilTimer* timer);
    void dealTimer(UtilTimer* timer, int sockfd);
    bool dealClientData();
    bool dealWithSignal(bool &timeout, bool &stopServer);
    void dealWithRead(int sockfd);
    void dealWithWrite(int sockfd);

public:
    //基础变量
    int m_port;
    char* m_root;//存储的是资源路径
    int m_asyncLogWrite;
    int m_closeLog;
    int m_concurrencyModel;
    int m_pipefd[2];
    int m_epollfd;
    HttpConn* m_users;

    //数据库相关
    ConnectionPool* m_connPool;
    string m_user;
    string m_password;
    string m_databaseName;
    int m_sqlNum;

    //线程池相关
    Threadpool<HttpConn>* m_pool;
    int m_threadNum;

    //epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];
    int m_listenfd;
    int m_optLinger;
    int m_trigMode;
    int m_listenTrigMode;
    int m_connTrigMode;

    //定时器相关
    ClientData* m_usersTimer;
    Utils m_utils;
};

#endif