/* 并发模式：半同步/半反应堆+线程池 
    使用一个工作队列（请求队列）完全解耦主线程和工作线程：主线程往工作队列中插入任务（http），工作线程通过竞争（信号量）来取得任务并执行它。
    1. reactor模式和同步io模拟的proactor模式均实现（两者区别在于io操作是由主线程还是工作线程进行）
    2. 半同步/半反应堆并发模式，是半同步/半异步并发模式的一种（这里的同步，异步和io同步/异步中的概念不同）
    3. 线程池，避免了创建和销毁线程的开销，另外并发模式也需要线程池的支持
*/

#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <list>
#include "locker.h"
#include <exception>
#include "sqlconnpool.h"
#include "httpconn.h"
#include "log.h"

template<typename T> //任务类型
class Threadpool {
public:
    Threadpool(int concurrencyModel, ConnectionPool* connpool, int threadNum = 8, int maxRequest = 10000);
    ~Threadpool();
    bool append(T* request, int reactorState);//reactor模式下往请求队列中追加任务，reactorState标记是读/写任务
    bool appendProactor(T* request);//proactor模式下追加任务到请求队列
private:
    //为什么设置为静态成员函数？？？？？
    static void* worker(void* arg);//工作线程运行的函数，不断从工作队列中取出任务并执行
    void run();//worker中实际执行任务的函数


private:
    int m_threadNum; //线程个数
    int m_maxRequest; //请求队列中允许的最大请求数
    pthread_t* m_threads; //描述线程池的数组，大小为m_thread_num
    std::list<T*> m_requestQueue;//请求/工作/任务队列
    Locker m_queueLocker; //保护请求队列的互斥锁
    Sem m_queueState;  //是否有任务需要处理，初值为0，刚开始没有任务需要处理
    int m_concurrencyModel; //并发模式，1：reactor
    ConnectionPool* m_connPool;//连接池
};

#endif