/* 线程同步机制封装：信号量/条件变量/互斥锁 */

//c/c++中常见头文件保护机制，C/C++ 语言标准都要求头文件只能被包含一次,否则会导致未定义的行为
#ifndef LOCKER_H
#define LOCKER_H

#include<semaphore.h>
#include<exception>
#include<pthread.h>

/* *****信号量***** */
class Sem {
public:
    Sem();//用于初始化一个值为0的信号量
    Sem(int num);//用于初始化一个值为num的信号量
    ~Sem();//摧毁信号量

    bool wait();//p操作，申请（等待）信号量
    bool post();//v操作，释放信号量，用于唤醒等待的线程

private:
    sem_t m_sem;//信号量标识符
};

/* *****互斥锁***** */
class Locker {
public:
    Locker();//初始化一把互斥锁
    ~Locker();//摧毁互斥锁

    bool lock();//上锁
    bool unlock();//解锁
    pthread_mutex_t* getMutexLocker();//获取创建的锁

private:
    pthread_mutex_t m_mutex;//互斥锁标识符
};

/* *****条件变量***** */
class Cond {
public:
    Cond();//初始化条件变量
    ~Cond();//摧毁条件变量

    bool wait(pthread_mutex_t* mutex);//阻塞等待条件变量,要配合互斥锁使用
    bool timedWait(pthread_mutex_t* mutex, struct timespec t);//非阻塞（定时）等待条件变量，需要互斥锁
    bool broadcast();//唤醒所有等待条件变量的线程
    bool signal();//唤醒一个等待条件变量的线程

private:
    pthread_cond_t m_cond;//条件变量标识符
};

#endif