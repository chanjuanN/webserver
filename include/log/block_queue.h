/* 循环数组实现阻塞队列， m_back = (m_back+1)%m_max_size 
线程安全，每个操作前后加互斥锁，操作完成，解锁 */

//用于异步日志系统

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include "locker.h"
#include <stdlib.h>
#include <sys/time.h>
#include <string>

template <class T>
class BlockQueue {
public:
    BlockQueue(int maxSize=1000);
    ~BlockQueue();
    void clear();
    bool isFull();
    bool isEmpty();
    bool front(T &value);
    bool back(T &value);
    int size();
    int maxSize();
    bool push(const T &item);
    bool pop(T &item);
    bool pop(T &item, int msTimeout);

private:
    Locker m_mutex;//配合条件变量使用的互斥锁，保证条件变量的原子操作
    Cond m_cond;
    T* m_array;
    int m_size;
    int m_maxSize;
    int m_front;//循环队列，所以需要一个队首指针和一个队尾指针
    int m_back;
};


#endif
