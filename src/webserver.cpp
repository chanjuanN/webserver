#include "webserver.h"

WebServer::WebServer() {
    m_users = new HttpConn[MAX_FD];//任何文件描述符预先分配httpconn对象

    char serverPath[200];
    getcwd(serverPath, 200); //todo 是不是得改
    // printf("cwd:%s\n", serverPath); //cwd:/mnt/data/ncj/project_ncj/mywebserver
    char root[6] = "/root";
    m_root = (char* )malloc(strlen(serverPath)+strlen(root)+1);//存储的是资源路径
    strcpy(m_root, serverPath);
    strcat(m_root, root);

    //每个连接的定时器
    m_usersTimer = new ClientData[MAX_FD];
}

WebServer::~WebServer() {
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[0]);
    close(m_pipefd[1]);
    delete[] m_users;
    delete[] m_usersTimer;
    delete m_pool;//线程池
}

//基础变量赋值操作
void WebServer::init(int port, string user, string password, string databaseName, int asyncLogWrite, int optLinger, 
        int trigMode, int sqlNum, int threadNum, int closeLog, int concurrencyModel) {
    m_port = port;
    m_user = user;
    m_password = password;
    m_databaseName = databaseName;
    m_asyncLogWrite = asyncLogWrite;
    m_optLinger = optLinger;
    m_trigMode = trigMode;
    m_sqlNum = sqlNum;
    m_threadNum = threadNum;
    m_closeLog = closeLog;
    m_concurrencyModel = concurrencyModel;
}

//线程池，请求队列中的任务请求类型是http_conn
void WebServer::threadPool() {
    //作者说线程池也可以用单例模式
    m_pool = new Threadpool<HttpConn>(m_concurrencyModel, m_connPool, m_threadNum);
}

//初始化数据库连接池
void WebServer::sqlPool() {
    m_connPool = ConnectionPool::GetInstance();
    m_connPool->init("localhost", m_user, m_password, m_databaseName, 3306, m_sqlNum, m_closeLog);
    //初始化从数据库读取表(user,passwd)，存到一个map中，后续先在map中查询，只有注册时才需要访问数据库
    m_users->initMysqlResult(m_connPool);
}

void WebServer::logWrite() {
    if(0==m_closeLog) {
        if(1==m_asyncLogWrite) {
            //异步写入
            Log::getInstance()->init("./ServerLog", m_closeLog, 2000, 800000, 800);
        } else { //同步
            Log::getInstance()->init("./ServerLog", m_closeLog, 2000, 800000, 0);
        }
    }
}

void WebServer::trigMode() { //lt/et
    if(0==m_trigMode) {
        m_listenTrigMode = 0;
        m_connTrigMode = 0;
    } else if(1==m_trigMode) {
        m_listenTrigMode = 0;
        m_connTrigMode = 1;
    } else if(2==m_trigMode) {
        m_listenTrigMode = 1;
        m_connTrigMode = 0;
    } else if(3==m_trigMode) {
        m_listenTrigMode = 1;
        m_connTrigMode = 1;
    } else { //默认lt
        m_listenTrigMode = 0;
        m_connTrigMode = 0;
    }
}

void WebServer::eventListen() {

    //网络编程基础步骤

    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd>=0);

    //优雅关闭连接
    // 如果 m_OPT_LINGER 为 0,则创建一个 linger 结构体 tmp。
        // tmp.l_onoff = 0，表示关闭 linger 选项。当 linger 选项关闭时(l_onoff=0)，关闭套接字时会【立即返回】,OS 会在后台异步地尝试发送剩余数据,但如果 1 秒内发送不完,则会强制关闭套接字。
            //当 linger 选项开启时(l_onoff=1)，关闭套接字时会【阻塞】,直到数据发送完毕或超时 1 秒。
            //开启linger选项可以尽量保证数据发送完成，因为会阻塞，即更优雅，但由于网络状况等其他原因，不能保证百分百发送完数据。
        // tmp.l_linger = 1，表示等待 1 秒钟后强制关闭套接字。
    if(0==m_optLinger) {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    } else {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in addr;
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    //INADDR_ANY它是一个预定义的常量,代表 "所有可用的 IP 地址"。即主机自身拥有的 IPv4 地址。比如一台主机有多个网卡,每个网卡都有一个 IPv4 地址,那么这些地址都是可用的。
    // 将 address.sin_addr.s_addr 设置为 INADDR_ANY 意味着服务端 Socket 会监听所有可用的 IP 地址,而不需要指定一个具体的 IP 地址。
    // 这样做的好处是,当服务端主机有多个网卡或 IP 地址时,客户端就可以连接到任意一个可用的 IP 地址。
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(m_port);

    int flag = 1;
    //即使端口处于 TIME_WAIT 状态,SO_REUSEADDR允许 新的 Socket 也可以顺利绑定该端口。
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(m_listenfd, (struct sockaddr* )&addr, sizeof(addr));
    assert(ret>=0);
    ret = listen(m_listenfd, 5);
    assert(ret>=0);

    m_utils.init(TIMESLOT);

    //epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd!=-1);

    m_utils.addFd(m_epollfd, m_listenfd, false, m_listenTrigMode);
    HttpConn::m_epollfd = m_epollfd;

    //统一事件源--信号事件
    /*  信号处理函数是由操作系统内核来调用的,而不是由用户程序主动调用。
    把信号处理逻辑放到主线程的主循环中，
    而信号处理函数被触发时，只是简单通知主循环程序接受到信号，并把信号值通过“管道”传递给主循环，
    主循环再通过具体的信号值执行对应逻辑代码
    */
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);//双向管道
    assert(ret!=-1);
    m_utils.setNonBlocking(m_pipefd[1]);
    m_utils.addFd(m_epollfd, m_pipefd[0], false, 0);//主线程通过io多路复用监听管道读端，来收信号，即统一事件源
    m_utils.addSig(SIGPIPE, SIG_IGN);
    m_utils.addSig(SIGALRM, m_utils.sigHandler, false);
    m_utils.addSig(SIGTERM, m_utils.sigHandler, false);
    alarm(TIMESLOT);//开启定时器

    Utils::m_uPipefd = m_pipefd;
    Utils::m_uEpollfd = m_epollfd;
}

void WebServer::eventLoop() {
    bool timeout = false;
    bool stopServer = false;

    while(!stopServer) {
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if(number<0 && errno!=EINTR) {
            LOG_ERROR("%s", "epoll failure");
            break;
        }
        for(int i=0; i<number; i++) {
            int sockfd = events[i].data.fd;

            //处理新到的客户连接：tcp+http连接
            if(sockfd==m_listenfd) {
                bool flag = dealClientData();
                if(false==flag) {
                    continue;
                }
            } else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                //客户端关闭连接了，服务器也关闭连接
                UtilTimer* timer = m_usersTimer[sockfd].timer;
                dealTimer(timer, sockfd);
            } else if((sockfd==m_pipefd[0]) && (events[i].events & EPOLLIN)) {
                //处理信号
                bool flag = dealWithSignal(timeout, stopServer);
                if(false==flag) {
                    LOG_ERROR("%s", "deal client data failure");
                }
            } else if(events[i].events & EPOLLIN) {
                //处理客户连接上的数据
                dealWithRead(sockfd);
            } else if(events[i].events & EPOLLOUT) {
                //将服务器响应返回给客户端
                dealWithWrite(sockfd);
            }
        }
        if(timeout) {
            m_utils.timerHandler();
            LOG_INFO("%s", "timer tick");
            timeout = false;
        }
    }

}


//创建一个http连接后，设置定时器
void WebServer::timer(int connfd, struct sockaddr_in clientAddr) {
    m_users[connfd].init(connfd, clientAddr, m_root, m_connTrigMode, m_closeLog, m_user, m_password, m_databaseName);

    m_usersTimer[connfd].address = clientAddr;
    m_usersTimer[connfd].sockfd = connfd;
    UtilTimer* timer = new UtilTimer;//创造一个未初始化的类实例，不执行构造函数
    timer->m_userData = &m_usersTimer[connfd];
    timer->cbFunc = cbFunc;
    time_t cur = time(nullptr);
    timer->m_expire = cur + 3*TIMESLOT;
    m_usersTimer[connfd].timer = timer;
    m_utils.m_timerLst.addTimer(timer);
}

//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表上的位置进行调整
void WebServer::adjustTimer(UtilTimer* timer) {
    time_t cur = time(nullptr);
    timer->m_expire = cur + 3*TIMESLOT;
    m_utils.m_timerLst.adjustTimer(timer);
}

//处理定时任务
void WebServer::dealTimer(UtilTimer* timer, int sockfd) {
    timer->cbFunc(&m_usersTimer[sockfd]);//长时间没有使用，就关闭http连接
    if(timer) {
        m_utils.m_timerLst.delTimer(timer);
    }
    LOG_INFO("close fd %d", m_usersTimer[sockfd].sockfd);
}

bool WebServer::dealClientData() {
    struct sockaddr_in clientAddress;
    socklen_t clientAddrLen = sizeof(clientAddress);
    if(0==m_listenTrigMode) { //LT
        int connfd = accept(m_listenfd, (struct sockaddr* )&clientAddress, &clientAddrLen);
        if(connfd<0) {
            LOG_ERROR("%s: errno is %d", "accept error", errno);
            return false;
        }
        if(HttpConn::m_userCount>=MAX_FD) {
            m_utils.showError(connfd, "internal server busy");
            LOG_ERROR("%s", "internal server busy");
            return false;
        }
        timer(connfd, clientAddress);//创建一个http连接，并添加定时器
    } else {
        while(true) {
            int connfd = accept(m_listenfd, (struct sockaddr* )&clientAddress, &clientAddrLen);
            if(connfd<0) {
                LOG_ERROR("%s: errno is %d", "accept error", errno);
                break;
            }
            if(HttpConn::m_userCount>=MAX_FD) {
                m_utils.showError(connfd, "internal server busy");
                LOG_ERROR("%s", "internal server busy");
                break;
            }
            timer(connfd, clientAddress);//创建一个http连接，并添加定时器   
        }
        return false;
    }
    return true;
}

bool WebServer::dealWithSignal(bool &timeout, bool &stopServer) {
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if(ret==-1) {
        return false;
    } else if(ret==0) {
        return false;
    } else {
        for(int i=0; i<ret; i++) {
            switch(signals[i]) {
                case SIGALRM: {
                    timeout = true;
                    break;
                }
                case SIGTERM: {
                    stopServer = true;
                    break;
                }
            }
        }
    }
    return true;
}


/*Reactor 是一种同步、事件驱动的 I/O 模型。它由一个事件分发器(Reactor)负责监听 I/O 事件,并根据事件类型调用相应的事件处理器(Handler)来处理 I/O 操作。
    Proactor 是一种异步、事件驱动的 I/O 模型。它由一个事件分发器(Proactor)负责启动异步 I/O 操作,并在操作完成时通知相应的事件处理器(Handler)。*/

/* IO操作包括 内核区数据就绪 和 用户/内核之间的数据拷贝 两部分
无论采用哪种方式,实现异步 I/O 的核心思想都是: 
    应用程序不需要阻塞等待 I/O 操作完成。
    当 I/O 操作完成时,应用程序会得到相应的通知。
    应用程序根据通知执行相应的逻辑,避免了同步 I/O 的性能瓶颈。    
*/  
void WebServer::dealWithRead(int sockfd) {
    UtilTimer* timer = m_usersTimer[sockfd].timer;

    //reactor
    if(1==m_concurrencyModel) {
        //若有数据传输，则将定时器往后延迟3个单位：3*TIMESLOT
        //并对新的定时器在链表上的位置进行调整
        if(timer) {
            adjustTimer(timer);
        }
        //若监测到读事件，将该事件放入请求队列(线程池中定义的请求队列)---读写数据，处理客户请求均在工作线程worker中完成
        m_pool->append(m_users+sockfd, 0);//读

        while(true) { //------处理长时间未使用的http连接
            if(1==m_users[sockfd].m_improv) {  //请求被读/写后，improv变量置为1
                if(1==m_users[sockfd].m_timerFlag) { //如果该请求没有任务需要处理，就会进行定时器相关操作----关闭长时间未使用的http连接
                    dealTimer(timer, sockfd);
                    m_users[sockfd].m_timerFlag = 0;
                }
                m_users[sockfd].m_improv = 0;
                break;
            }
        }
    } else {
        //proactor---是同步io模拟的proactor(真正的proactor是异步io)
        if(m_users[sockfd].readOnce()) {  //----所有io操作都交给主线程处理
            LOG_INFO("deal with the client(%s)", inet_ntoa(m_users[sockfd].getAddress()->sin_addr));
            //若监测到读事件，将该事件放入请求队列-----工作线程仅仅负责业务逻辑，即处理客户请求
            m_pool->appendProactor(m_users+sockfd);
            if(timer) {
                adjustTimer(timer);
            }
        } else { //如果该请求没有任务需要处理，就会进行定时器相关操作----关闭长时间未使用的http连接
            dealTimer(timer, sockfd);
        }
    }
}

void WebServer::dealWithWrite(int sockfd) {
    UtilTimer* timer = m_usersTimer[sockfd].timer;
    if(1==m_concurrencyModel) {
        if(timer) {
            adjustTimer(timer);
        }
        m_pool->append(m_users+sockfd, 1);

        while(true) {
            if(1==m_users[sockfd].m_improv) {
                if(1==m_users[sockfd].m_timerFlag) {
                    dealTimer(timer, sockfd);
                    m_users[sockfd].m_timerFlag = 0;
                }
                m_users[sockfd].m_improv = 0;
                break;
            }
        }
    } else {
        if(m_users[sockfd].write()) {
            // inet_ntoa（）用于将 IPv4 地址从网络字节序转换为点分十进制字符串的函数
            LOG_INFO("send data to the client(%s)", inet_ntoa(m_users[sockfd].getAddress()->sin_addr));
            if(timer) {
                adjustTimer(timer);
            }
        } else {
            dealTimer(timer, sockfd);
        }
    }
}