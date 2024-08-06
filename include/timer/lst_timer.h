/* 定时器处理非活动连接
===============
由于非活跃连接占用了连接资源，严重影响服务器的性能，通过实现一个服务器定时器，处理这种非活跃连接，释放连接资源。利用alarm函数周期性地触发SIGALRM信号,该信号的信号处理函数利用管道通知主循环执行定时器链表上的定时任务.
> * 统一事件源
> * 基于升序链表的定时器
> * 处理非活动连接 */

#ifndef LST_TIMER_H
#define LST_TIMER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <fcntl.h>
#include <sys/epoll.h>
// #include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "httpconn.h"

class UtilTimer;

struct ClientData {
    sockaddr_in address;
    int sockfd;
    UtilTimer* timer;
};

//定时器类
class UtilTimer {
public:
    UtilTimer() {};

public:
    time_t m_expire;

    void (*cbFunc)(ClientData* );
    ClientData* m_userData;
    UtilTimer* m_prev;
    UtilTimer* m_next;
};

void cbFunc(ClientData* userData);

class SortTimerLst {
public:
    SortTimerLst();
    ~SortTimerLst();

    void addTimer(UtilTimer* timer);
    void adjustTimer(UtilTimer* timer);
    void delTimer(UtilTimer* timer);
    void tick();

private:
    void addTimer(UtilTimer* timer, UtilTimer* lstHead);

    UtilTimer* m_head;
    UtilTimer* m_tail;
};


//定时器的工具类
class Utils {
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    int setNonBlocking(int fd);
    void addFd(int epollfd, int fd, bool oneShot, int trigMode);
    static void sigHandler(int sig);
    void addSig(int sig, void(handler)(int), bool restart=true);
    void timerHandler();
    void showError(int connfd, const char* info);

public:
    static int* m_uPipefd;
    SortTimerLst m_timerLst;
    static int m_uEpollfd;
    int m_timeslot;
};

#endif