#include "block_queue.h"

template <class T>
BlockQueue<T>::BlockQueue(int maxSize) {
    if(maxSize<=0)
        exit(-1);
    
    m_maxSize = maxSize;
    m_size = 0;
    m_front = -1;
    m_back = -1;
    m_array = new T[m_maxSize];
}

template <class T>
BlockQueue<T>::~BlockQueue() {
    m_mutex.lock();
    if(m_array!=nullptr) {
        delete[] m_array;
    }
    m_mutex.unlock();
}

template <class T>
void BlockQueue<T>::clear() {
    m_mutex.lock();
    m_size = 0;
    m_front = -1;
    m_back = -1;
    m_mutex.unlock();
}

template <class T>
bool BlockQueue<T>::isFull() {
    m_mutex.lock();
    if(m_size>=m_maxSize) {
        m_mutex.unlock();
        return true;
    }
    m_mutex.unlock();
    return false;
}

template <class T>
bool BlockQueue<T>::isEmpty() {
    m_mutex.lock();
    if(0==m_size) {
        m_mutex.unlock();
        return true;
    }
    m_mutex.unlock();
    return false;
}

template <class T>
bool BlockQueue<T>::front(T &value) {
    m_mutex.lock();
    if(0==m_size) {
        m_mutex.unlock();
        return false;
    }
    value = m_array[m_front];
    m_mutex.unlock();
    return true;
}

template <class T>
bool BlockQueue<T>::back(T &value) {
    m_mutex.lock();
    if(0==m_size) {
        m_mutex.unlock();
        return false;
    }
    value = m_array[m_back];
    m_mutex.unlock();
    return true;
}

template <class T>
int BlockQueue<T>::size() {
    int temp = 0;
    m_mutex.lock();
    temp = m_size;//要固定下来这个值，因为解锁后还可能发生变化
    m_mutex.unlock();
    return temp;
}

template <class T>
int BlockQueue<T>::maxSize() {
    int temp = 0;
    m_mutex.lock();
    temp = m_maxSize;
    m_mutex.unlock();
    return temp;
}

//往队列添加元素，需要将所有使用队列的线程唤醒
//当有元素push进队列,相当于生产者生产了一个元素
//若当前没有线程等待条件变量,则唤醒无意义
template <class T>
bool BlockQueue<T>::push(const T &item) {
    m_mutex.lock();
    if(m_size>=m_maxSize) {
        m_cond.broadcast();
        m_mutex.unlock();
        return false;
    }
    m_back = (m_back+1)%m_maxSize;//循环数组
    m_array[m_back] = item;
    m_size++;
    m_cond.broadcast();
    m_mutex.unlock();
    return true;
}

template <class T>
bool BlockQueue<T>::pop(T &item) {
    m_mutex.lock();
    while(m_size<=0) { //pop时,如果当前队列没有元素,将会等待条件变量

        if(!m_cond.wait(m_mutex.getMutexLocker())) {
            m_mutex.unlock();
            return false;
        }
    }
    m_front = (m_front+1)%m_maxSize;//当前队首指针+1才是队列的第一个元素
    item = m_array[m_front];
    m_size--;
    m_mutex.unlock();
    return true;
}

template <class T>
bool BlockQueue<T>::pop(T &item, int msTimeout) {
    struct timespec t = {0, 0};
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);
    m_mutex.lock();
    if(m_size<=0) {
        t.tv_sec = now.tv_sec + msTimeout/1000;
        t.tv_nsec = (msTimeout%1000)*1000000;//毫秒到纳秒
        if(!m_cond.timedWait(m_mutex.getMutexLocker(), t)) {
            m_mutex.unlock();
            return false;
        }
    }
/*     if(m_size<=0) {
        m_mutex.unlock();
        return false;
    } */ //todo ,感觉是多余的
    m_front = (m_front+1)%m_maxSize;
    item = m_array[m_front];
    m_size--;
    m_mutex.unlock();
    return true;
}

// 显式实例化
template class BlockQueue<std::string>;