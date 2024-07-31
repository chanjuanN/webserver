#include"locker.h"

/* *****信号量***** */
//用于初始化一个值为0的信号量
Sem::Sem() {
    //第一个0代表该信号量只能在创建它的进程中使用，不能在进程间共享
    //第二个0代表信号量初值为0
    if(sem_init(&m_sem, 0, 0)!=0) {
        throw std::exception();
    }
}

//用于初始化一个值为num的信号量
Sem::Sem(int num) {
    if(sem_init(&m_sem, 0, num)!=0) {
        throw std::exception();
    }
}

//摧毁信号量
Sem::~Sem() {
    sem_destroy(&m_sem);
}

//p操作，申请（等待）信号量
bool Sem::wait() {
    return sem_wait(&m_sem)==0;
}

//v操作，释放信号量，用于唤醒等待的线程
bool Sem::post() {
    return sem_post(&m_sem)==0;
}


/* *****互斥锁***** */
//初始化一把互斥锁
Locker::Locker() {
    if(pthread_mutex_init(&m_mutex, nullptr)!=0) {
        throw std::exception();
    }
}

//摧毁互斥锁
Locker::~Locker() {
    pthread_mutex_destroy(&m_mutex);
}

//上锁
bool Locker::lock() {
    return pthread_mutex_lock(&m_mutex)==0;
}

//解锁
bool Locker::unlock() {
    return pthread_mutex_unlock(&m_mutex)==0;
}

//获取创建的锁
pthread_mutex_t* Locker::getMutexLocker() {
    return &m_mutex;
}

/* *****条件变量***** */
//初始化条件变量
Cond::Cond() {
    if(pthread_cond_init(&m_cond, nullptr)!=0) {
        throw std::exception();
    }
}

//摧毁条件变量
Cond::~Cond() {
    pthread_cond_destroy(&m_cond);
}

//阻塞等待条件变量,要配合互斥锁使用
bool Cond::wait(pthread_mutex_t* mutex) {
    int ret = 0;
    ret = pthread_cond_wait(&m_cond, mutex);
    return ret==0;
}

//非阻塞（定时）等待条件变量，需要互斥锁
bool Cond::timedWait(pthread_mutex_t* mutex, struct timespec t) {
    return pthread_cond_timedwait(&m_cond, mutex, &t)==0;
}

//唤醒所有等待条件变量的线程
bool Cond::broadcast() {
    return pthread_cond_broadcast(&m_cond)==0;
}

//唤醒一个等待条件变量的线程
bool Cond::signal() {
    return pthread_cond_signal(&m_cond)==0;
}