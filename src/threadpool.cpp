#include "threadpool.h"

template <typename T>
Threadpool<T>::Threadpool(int concurrencyModel, ConnectionPool* connpool, int threadNum, int maxRequest):
    m_concurrencyModel(concurrencyModel), 
    m_threadNum(threadNum),
    m_maxRequest(maxRequest),
    m_threads(nullptr),
    m_connPool(connpool) {
    if(threadNum<=0 || maxRequest<=0) {
        throw std::exception();
    }
    m_threads = new pthread_t[m_threadNum];
    if(!m_threads) {
        throw std::exception();
    }

    for(int i=0; i<m_threadNum; i++) {
        //创建线程：
        /*
            线程号
            线程属性：NULL(默认属性)
            工作函数：线程创建好以后，内核会控制相应的内核线程去执行该函数
            参数：传递给工作函数的参数，这里传递的是线程池本身
        */
        if(pthread_create(m_threads+i, nullptr, worker, this)!=0) {
            delete[] m_threads;
            throw std::exception();
        }
        //线程属性中有个detachstate
        //这里使用pthread_detach()函数将子线程设置为detached态, 不需要主线程等待回收其资源，而是自己主动释放资源
        if(pthread_detach(m_threads[i])) {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
Threadpool<T>::~Threadpool() {
    delete[] m_threads;
}

template<typename T>
//reactor和proactor的区别，reactor需要工作线程自己处理io操作，所以reactorState用于标记是读任务还是写任务，proactor不使用该变量
bool Threadpool<T>::append(T* request, int reactorState) { //reactor模式下往请求队列中追加任务，reactorState标记是读/写任务
    m_queueLocker.lock();
    if(m_requestQueue.size()>=m_maxRequest) {
        m_queueLocker.unlock();
        return false;
    }
    request->m_reactorState = reactorState;
    m_requestQueue.push_back(request);
    m_queueLocker.unlock();
    m_queueState.post();//唤醒等待任务的工作线程
    return true;
}

template<typename T>
bool Threadpool<T>::appendProactor(T* request) { //proactor模式下追加任务到请求队列
    m_queueLocker.lock();
    if(m_requestQueue.size()>=m_maxRequest) {
        m_queueLocker.unlock();
        return false;
    }
    m_requestQueue.push_back(request);
    m_queueLocker.unlock();
    m_queueState.post();
    return true;
}

template<typename T>
void* Threadpool<T>::worker(void* arg) { //工作线程运行的函数，不断从工作队列中取出任务并执行
    Threadpool* pool = (Threadpool* )arg;
    pool->run();
    return pool;
}

template<typename T>
void Threadpool<T>::run() { //worker中实际执行任务的函数
    while(true) {
        m_queueState.wait();//等待任务
        m_queueLocker.lock();
        if(m_requestQueue.empty()) {
            m_queueLocker.unlock();
            continue;
        }

        T* request = m_requestQueue.front();
        m_requestQueue.pop_front();//带头双向循环链表
        m_queueLocker.unlock();

        if(!request) {
            continue;
        }
        if(1==m_concurrencyModel) { //reactor模式 工作线程负责io
            if(0==request->m_reactorState) { //读请求
                if(request->readOnce()) {
                    request->m_improv = 1;
                    //连接数据库操作
                    ConnectionRAII mysqlConn(&request->m_mysql, m_connPool);
                    request->process();//处理请求
                } else {
                    request->m_improv = 1;
                    request->m_timerFlag = 1;//处理定时器任务
                }
            } else { //写请求
                if(request->write()) {
                    request->m_improv = 1;
                } else {
                    request->m_improv = 1;
                    request->m_timerFlag = 1;
                }
            }
        } else { //proactor模式
            //连接数据库
            ConnectionRAII mysqlConn(&request->m_mysql, m_connPool);
            request->process();//只可能出现处理请求任务，因为io操作在主线程中完成
        }

    }
}

//模板实例化
template class Threadpool<HttpConn>;