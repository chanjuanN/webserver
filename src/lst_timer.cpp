#include "lst_timer.h"

SortTimerLst::SortTimerLst() {
    m_head = nullptr;
    m_tail = nullptr;
}

SortTimerLst::~SortTimerLst() {
    UtilTimer* tmp = m_head;
    while(tmp) {
        m_head = tmp->m_next;
        delete tmp;
        tmp = m_head;
    }
}

void SortTimerLst::addTimer(UtilTimer* timer) {
    if(!timer) {
        return;
    }

    if(!m_head) {
        m_head = m_tail = timer;
        return;
    }
    if(timer->m_expire < m_head->m_expire) {
        timer->m_next = m_head;
        m_head->m_prev = timer;
        m_head = timer;
        return;
    }
    addTimer(timer, m_head);
}

void SortTimerLst::adjustTimer(UtilTimer* timer) {
    if(!timer) {
        return;
    }
    UtilTimer* tmp = timer->m_next;
    if(!tmp || (timer->m_expire < tmp->m_expire)) {
        return;//不用调整位置
    }
    if(timer==m_head) {
        m_head = m_head->m_next;
        m_head->m_prev = nullptr;
        timer->m_next = nullptr;
        addTimer(timer, m_head);
    } else {
        timer->m_prev->m_next = timer->m_next;
        timer->m_next->m_prev = timer->m_prev;
        addTimer(timer, timer->m_next);
    }
}

void SortTimerLst::delTimer(UtilTimer* timer) {
    if(!timer) {
        return;
    }
    if(timer==m_head && timer==m_tail) {
        delete timer;
        m_head = nullptr;
        m_tail = nullptr;
        return;
    }
    if(timer==m_head) {
        m_head = m_head->m_next;
        m_head->m_prev = nullptr;
        delete timer;
        return;
    }
    if(timer==m_tail) {
        m_tail = m_tail->m_prev;
        m_tail->m_next = nullptr;
        delete timer;
        return;
    }
    timer->m_prev->m_next = timer->m_next;
    timer->m_next->m_prev = timer->m_prev;
    delete timer;
}

void SortTimerLst::tick() {
    if(!m_head) {
        return;
    }

    time_t cur = time(nullptr);
    UtilTimer* tmp = m_head;
    while(tmp) {
        if(cur<tmp->m_expire) {
            break;
        }
        tmp->cbFunc(tmp->m_userData);//执行定时任务
        m_head = tmp->m_next;
        if(m_head) {
            m_head->m_prev = nullptr;
        }
        delete tmp;
        tmp = m_head;
    }
}

void SortTimerLst::addTimer(UtilTimer* timer, UtilTimer* lstHead) {
    UtilTimer* prev = lstHead;
    UtilTimer* tmp = prev->m_next;
    while(tmp) {
        if(timer->m_expire < tmp->m_expire) {
            prev->m_next = timer;
            timer->m_next = tmp;
            tmp->m_prev = timer;
            timer->m_prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->m_next;
    }
    if(!tmp) {
        prev->m_next = timer;
        timer->m_prev = prev;
        timer->m_next = nullptr;
        m_tail = timer;
    }
}

void Utils::init(int timeslot) {
    m_timeslot = timeslot;
}

int Utils::setNonBlocking(int fd) {
    int oldOption = fcntl(fd, F_GETFL);
    int newOption = oldOption | O_NONBLOCK;
    fcntl(fd, F_GETFL, newOption);
    return oldOption;
}

void Utils::addFd(int epollfd, int fd, bool oneShot, int trigMode) {
    epoll_event event;
    event.data.fd = fd;

    if(1==trigMode) {
        //et
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    } else {
        event.events = EPOLLIN | EPOLLRDHUP;
    }

    if(oneShot) {
        event.events |= EPOLLONESHOT;
    }

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setNonBlocking(fd);
}

//信号处理函数
void Utils::sigHandler(int sig) {
    int saveErrno = errno;  //为保证函数的可重入性，保留原来的errno
    int msg = sig;
    send(m_uPipefd[1], (char*)&msg, 1, 0);
    errno = saveErrno;
}

//设置信号函数
void Utils::addSig(int sig, void(handler)(int), bool restart) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));//sizeof是内置运算符，不是函数，不需要包含头文件
    sa.sa_handler = handler;
    if(restart) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, nullptr)!=-1);
}

// 定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timerHandler() {
    m_timerLst.tick();
    alarm(m_timeslot);
}

void Utils::showError(int connfd, const char* info) {
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int* Utils::m_uPipefd = 0;
int Utils::m_uEpollfd = -1;

void cbFunc(ClientData* userData) {
    epoll_ctl(Utils::m_uEpollfd, EPOLL_CTL_DEL, userData->sockfd, 0);
    assert(userData);
    close(userData->sockfd);
    HttpConn::m_userCount--;
}