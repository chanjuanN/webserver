#include "httpconn.h"

//定义http响应的一些状态信息
const char* ok200Title = "OK";
const char* error400Title = "BAD REQUEST";
const char* error400Form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error403Title = "FORBIDDEN";
const char* error403Form = "You do not have permission to get file from this server.\n";
const char* error404Title = "NOT FOUND";
const char* error404Form = "The requested file was not found on this server.\n";
const char* error500Title = "INTERNAL ERROR";
const char* error500Form = "There was an unusual problem serving the request file.\n";

/*epoll相关*/
//设置非阻塞io
int setNonBlocking(int fd) {
    int oldOption = fcntl(fd, F_GETFL);
    int newOption = oldOption | O_NONBLOCK;
    fcntl(fd, F_SETFL, newOption);
    return oldOption;
}

//将内核事件表注册读事件,ET/LT模式,选择是否开启EPOLLONESHOT属性
void addFd(int epollfd, int fd, bool oneShot, int TrigMode) {
    epoll_event event;
    event.data.fd = fd;
    if(1==TrigMode) { //ET模式
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

//从内核事件表删除文件描述符
void delFd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

//将事件重置为ONESHOT
void modFd(int epollfd, int fd, int events, int TrigMode) {
    epoll_event event;
    event.data.fd = fd;
    if(1==TrigMode) {
        event.events = events | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    } else {
        event.events = events | EPOLLONESHOT | EPOLLRDHUP;
    }

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

//静态成员变量,类内声明,类外初始化
int HttpCoon::m_userCount = 0;
int HttpCoon::m_epollfd = -1;

HttpCoon::HttpCoon() {}

HttpCoon::~HttpCoon() {}

//初始化http连接 todo
void HttpCoon::init(int sockfd, const sockaddr_in &addr, char* root, int TrigMode, int closeLog, string user, string passwd, string sqlname) {
    //当浏览器出现连接重置时,可能是网站根目录出错或者http响应格式出错或者访问的文件中内容完全为空
    m_docRoot = root;
    m_TrigMode = TrigMode;
    m_closeLog = closeLog;
    m_sockfd = sockfd;
    m_address = addr;
    m_TrigMode = TrigMode;//todo

    strcpy(m_sqlUser, user.c_str());
    strcpy(m_sqlPasswd, passwd.c_str());
    strcpy(m_sqlName, sqlname.c_str());
    //初始化其他变量
    init();

    addFd(m_epollfd, m_sockfd, true, m_TrigMode);
    m_userCount++;
}

//初始化其他变量
void HttpCoon::init() {
    // m_mysql = nullptr; todo
    m_bytesToSend = 0;
    m_bytesHaveSend = 0;
    m_checkState = CHECK_STATE_REQUESTLINE;
    m_linger = false;//todo
    m_method = GET;
    m_url = nullptr;
    m_version = nullptr;
    m_contentLength = 0;
    m_host = nullptr;
    m_startLine = 0;
    m_checkedIdx = 0;
    m_readIdx = 0;
    m_writeIdx = 0;
    // m_cgi = 0;//POST?
    m_reactorState = 0;
    m_timerFlag = 0;//1：处理定时器任务,删除超时连接
    m_improv = 0;//reactor模式下，提醒主线程，工作线程已经完成io操作

    memset(m_readBuf, '\0', READ_BUFFER_SIZE);
    memset(m_writeBuf, '\0', WRITE_BUFFER_SIZE);
    memset(m_realFile, '\0', FILENAME_LEN);
}

//关闭一个连接,客户数减一
void HttpCoon::closeConn(bool realClose) {
    if(realClose && (m_sockfd!=-1)) {
        printf("close %d\n", m_sockfd);
        delFd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_userCount--;
    }
}

void HttpCoon::process() {
    HTTP_CODE readRet = processRead();
    if(readRet==NO_REQUEST) {
        modFd(m_epollfd, m_sockfd, EPOLLIN, m_TrigMode);
        return;
    }

    bool writeRet = processWrite(readRet);
    if(!writeRet) { //请求不对或未处理成功，都直接关闭连接
        closeConn();
    }
    //请求处理成功，发数据给客户端
    modFd(m_epollfd, m_sockfd, EPOLLOUT, m_TrigMode);
}

//循环读取客户数据，直到无数据可读或对方关闭连接
//非阻塞ET工作模式下，需要一次性将数据读完,因为只触发一次
bool HttpCoon::readOnce() {
    if(m_readIdx>=READ_BUFFER_SIZE) {
        return false;
    }
    int bytesRead = 0;

    //LT
    if(0==m_TrigMode) {
        /* 其中最后一个参数 flags 可以设置一些特殊的标志位,用于控制 recv() 函数的行为。常见的标志位有:
            MSG_PEEK: 窥探(peek)数据,不从缓冲区移除数据。
            MSG_OOB: 接收带外数据。
            MSG_WAITALL: 等待直到接收到所有请求的字节数。 
        为0表示默认的socket选项,只是简单地从 socket 中读取数据到 m_readBuf 缓冲区中,不设置任何特殊的标志位。*/
        bytesRead = recv(m_sockfd, m_readBuf+m_readIdx, READ_BUFFER_SIZE-m_readIdx, 0);
        if(bytesRead<=0) {
            return false;
        }
        m_readIdx += bytesRead;
        return true;
    } else { //ET
        while(true) {
            bytesRead = recv(m_sockfd, m_readBuf+m_readIdx, READ_BUFFER_SIZE-m_readIdx, 0);
            if(bytesRead==-1) {
                if(errno==EAGAIN || errno==EWOULDBLOCK) { //如果套接字被设置为非阻塞模式(non-blocking)，表示此时没有可读的数据
                    break;
                }
                return false;
            } else if(bytesRead==0) {
                return false;
            } else {
                m_readIdx += bytesRead;
            }
        }
        return true;
    }
}

bool HttpCoon::write() {
    int temp = 0;
    if(m_bytesToSend==0) {
        modFd(m_epollfd, m_sockfd, EPOLLIN, m_TrigMode);
        init();
        return true;
    }
    while(true) {
        //集中写,将多个分散内存的数据一起写到文件描述符中
        /*  fd 是要写入的文件描述符。
            iov 是一个指向 struct iovec 的指针数组,用于存储要写入的多个缓冲区。
            iovcnt 是 iov 数组中的元素个数。 */
        //这里我们有两个分散内存要集中写: m_iv[0]记录的是http应答内容 m_iv[1]记录的是请求的文件资源,
        //即完整的http应答包括:一个状态行/多个头部字段/一个空行和文档内容
        temp = writev(m_sockfd, m_iv, m_ivCount);
        if(temp<0) {
            if(errno==EAGAIN) {
                modFd(m_epollfd, m_sockfd, EPOLLOUT, m_TrigMode);
                return true;
            }
            unmap();
            return false;
        }
        m_bytesHaveSend += temp;
        m_bytesToSend -= temp;
        if(m_bytesHaveSend>=m_iv[0].iov_len) {
            m_iv[0].iov_len = 0;
            m_iv[0].iov_base = m_fileAddress + (m_bytesHaveSend-m_writeIdx);
            m_iv[0].iov_len = m_bytesToSend;
        } else {
            m_iv[0].iov_base = m_writeBuf + m_bytesHaveSend;
            m_iv[0].iov_len = m_iv[0].iov_len -m_bytesHaveSend;
        }

        if(m_bytesToSend<=0) {
            unmap();
            modFd(m_epollfd, m_sockfd, EPOLLIN, m_TrigMode);
            if(m_linger) {
                init();
                return true;
            } else {
                return false;
            }
        }
    }
}

sockaddr_in* HttpCoon::getAddress() {
    return &m_address;
}

// void HttpCoon::initMysqlResult();//todo

HttpCoon::HTTP_CODE HttpCoon::processRead() {
    LINE_STATUS lineStatus = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = nullptr;

    while((m_checkState==CHECK_STATE_CONTENT && lineStatus==LINE_OK) || ((lineStatus=parseLine())==LINE_OK)) {
        text = getLine();
        m_startLine = m_checkedIdx;
        // LOG_INFO("%s", text); todo

        switch(m_checkState) {
            case CHECK_STATE_REQUESTLINE: {
                ret = parseRequestLine(text);
                if(ret==BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER: {
                ret = parseHeaders(text);
                if(ret==BAD_REQUEST) {
                    return BAD_REQUEST;
                } else if(ret==GET_REQUEST) {
                    return doRequest();//get方法在这里处理请求
                } 
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parseContent(text);
                if(ret==GET_REQUEST) {
                    return doRequest();//post方法在这里处理请求
                }
                lineStatus = LINE_OPEN;//请求没有读完,继续读
                break;
            }
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

bool HttpCoon::processWrite(HTTP_CODE ret) {
    switch(ret) {
        case INTERNAL_ERROR: {
            addStatusLine(500, error500Title);
            addHeaders(strlen(error500Form));
            if(!addContent(error500Form)) {
                return false;
            }
            break;
        }
        case BAD_REQUEST: {
            addStatusLine(404, error404Title);
            addHeaders(strlen(error404Form));
            if(!addContent(error404Form)) {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST: {
            addStatusLine(403, error403Title);
            addHeaders(strlen(error403Form));
            if(!addContent(error403Form)) {
                return false;
            }
            break;
        }
        case FILE_REQUEST: {
            addStatusLine(200, ok200Title);
            if(m_fileStat.st_size!=0) {
                addHeaders(m_fileStat.st_size);
                m_iv[0].iov_base = m_writeBuf;
                m_iv[0].iov_len = m_writeIdx;
                m_iv[1].iov_base = m_fileAddress;
                m_iv[1].iov_len = m_fileStat.st_size;
                m_ivCount = 2;
                m_bytesToSend = m_writeIdx + m_fileStat.st_size;//所有要发送的数据大小
                return true;
            } else { //文件大小为0,发送空网页
                const char* okString = "<html><body></body></html>";
                addHeaders(strlen(okString));
                if(!addContent(okString))
                    return false;
            }
        }
        default:
            return false;
    }

    //不用发文件的话
    m_iv[0].iov_base = m_writeBuf;
    m_iv[0].iov_len = m_writeIdx;
    m_ivCount = 1;
    m_bytesToSend = m_writeIdx;
    return true;
}


/* 一个 HTTP POST 请求示例如下:省略 URL 中的 http:// 前缀是一种常见的做法,因为 HTTP 协议是默认使用的协议。但当使用其他协议时,就必须在 URL 中明确指定协议前缀。
POST /login.php HTTP/1.1
Host: www.example.com
User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.3
Content-Type: application/x-www-form-urlencoded
Content-Length: 15

username=admin&password=123456
*/

//解析http请求行(如POST /login.php HTTP/1.1),获得请求方法,目标url,及http版本
HttpCoon::HTTP_CODE HttpCoon::parseRequestLine(char* text) {
    m_url = strpbrk(text, " \t"); //返回第一个指向空格/\t的指针
    if(!m_url) {
        return BAD_REQUEST;
    }

    *m_url++ = '\0'; //这里的处理是把请求行变为“POST\0  /login.php HTTP/1.1", m_url = "  /login.php HTTP/1.1"这部分
    char* method = text;//method = "POST\0"
    if(strcasecmp(method, "GET")==0) { //不区分大小写，比较两个字符串大小
        m_method = GET;
    } else if(strcasecmp(method, "POST")==0) {
        m_method = POST;
        // m_cgi = 1;//POST方法
    } else {
        return BAD_REQUEST;//暂时不支持其他方法
    }
    //strspn() 函数用于计算字符串开头连续包含指定字符集中字符的长度
    m_url += strspn(m_url, " \t");//m_url = "/login.php HTTP/1.1"
    m_version = strpbrk(m_url, " \t");
    if(!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';//这里处理后，m_url就只指向网址了,m_version="  HTTP/1.1"
    m_version += strspn(m_version, " \t");
    if(strcasecmp(m_version, "HTTP/1.1")!=0) {
        return BAD_REQUEST;//只支持http/1.1
    }
    if(strncasecmp(m_url, "http://", 7)==0) {
        m_url += 7;//定位到文件部分,不要前面这些
        m_url = strchr(m_url, '/');//在 m_url 指向的字符串中查找第一个出现的斜杠字符 /。为了定位 URL 中的路径部分。
    }
    if(strncasecmp(m_url, "https://", 8)==0) {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    if(!m_url || m_url[0]!='/') {
        return BAD_REQUEST;
    }
    //当url为/时，显示判断界面；如果 URL 只有一个斜杠 /(表示访问网站的根目录),则将 URL 修改为 /judge.html。
    if(strlen(m_url)==1) {
        strcat(m_url, "judge.html");//当 URL 只有一个字符(即只有一个斜杠 /)时,会将 m_url 的内容修改为 "judge.html"。
    }
    m_checkState = CHECK_STATE_HEADER;//请求行处理完成，切换状态
    return NO_REQUEST;
}

//解析http请求的一个头部信息
HttpCoon::HTTP_CODE HttpCoon::parseHeaders(char* text) {
    if(text[0]=='\0') {
        //空行
        if(m_contentLength!=0) {
            //post方法,需要解析请求体
            m_checkState = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        } else {
            return GET_REQUEST;//get方法,返回请求资源即可
        }
    } else if(strncasecmp(text, "Connection:", 11)==0) {
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive")==0) {
            m_linger = true;//长连接
        }
    } else if(strncasecmp(text, "Content-length:", 15)==0) {
        text += 15;
        text += strspn(text, " \t");
        m_contentLength = atol(text);
    } else if(strncasecmp(text, "Host:", 5)==0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    } else {
        // LOG_INFO("oop! unknown header: %s", text); todo
    }
    return NO_REQUEST;
}

//判断http请求是否被完整读入
HttpCoon::HTTP_CODE HttpCoon::parseContent(char* text) {
    if(m_readIdx>=(m_contentLength+m_checkedIdx)) {
        text[m_contentLength] = '\0';
        //post请求中最后为输入的用户名和密码
        m_string = text;
        return GET_REQUEST;
    } else {
        return NO_REQUEST;
    }
}


HttpCoon::HTTP_CODE HttpCoon::doRequest() {
    strcpy(m_realFile, m_docRoot);
    int len = strlen(m_docRoot);
    printf("m_url: %s\n", m_url);//todo 删除
    const char* p = strrchr(m_url, '/');//在一个字符串中从后向前搜索指定的字符,并返回该字符最后出现的位置。

    // if(m_cgi==1 && (*(p+1)=='2' || *(p+1)=='3')) { //现在不用cgi,这个变量可以删除,感觉.todo
    //将用户名和密码提取出来
    //user=123&password=123        
    char name[100];
    char password[100];
    int i;
    for(i=5; m_string[i]!='&'; i++) {
        name[i-5] = m_string[i];
    }
    name[i-5] = '\0';

    int j = 0;
    for(i=i+10; m_string[i]!='\0'; i++) {
        password[j] = m_string[i];
    }
    password[j] = '\0';
    
    //处理post,注册/登陆,更改m_url内容
    if(*(p+1)=='3') {
        //注册
        char* sqlInsert = (char* )malloc(sizeof(char)*200);
        strcpy(sqlInsert, "INSERT INTO user(username, passwd) VALUES(");
        strcat(sqlInsert, "'");
        strcat(sqlInsert, name);
        strcat(sqlInsert, "', '");
        strcat(sqlInsert, password);
        strcat(sqlInsert, "')");

        //先在map表中查是否重名
        if(m_users.find(name)==m_users.end()) {
            m_lock.lock();
            /* mysql_query() 是一个用于执行 SQL 查询的函数，在旧版的 MySQL C API 中使用。它可以执行任何类型的 SQL 语句，不仅限于查询语句（SELECT），
            还可以执行插入（INSERT）、更新（UPDATE）、删除（DELETE）、创建表（CREATE TABLE）等。 */
            //执行mysql语句，新用户数据插入数据库表中
            int res = mysql_query(m_mysql, sqlInsert); //todo
            m_users.insert(pair<string, string>(name, password));//更新map表
            m_lock.unlock();

            if(!res) {
                strcpy(m_url, "/log.html");//注册成功返回登陆页面
            } else {
                strcpy(m_url, "registerError.html");//注册失败返回注册失败页面
            }
        } else {
            strcpy(m_url, "registerError.html");//重名返回注册失败
        }
    } else if(*(p+1)=='2') { //登陆, 直接在map表中校验信息
        if(m_users.find(name)!=m_users.end() && m_users[name]==password) {
            strcpy(m_url, "/welcome.html");//登陆成功
        } else {
            strcpy(m_url, "/logError.html");//登陆失败
        }
    } else if(*(p+1)=='0') { //以下都是get方法处理,返回所要资源,路经: m_realFile
        char* urlReal = (char*)malloc(sizeof(char)*200);
        strcpy(urlReal, "/register.html");
        strncpy(m_realFile+len, urlReal, strlen(urlReal));
        free(urlReal);
    } else if(*(p+1)=='1') { 
        char* urlReal = (char*)malloc(sizeof(char)*200);
        strcpy(urlReal, "/log.html");
        strncpy(m_realFile+len, urlReal, strlen(urlReal));
        free(urlReal);
    } else if(*(p+1)=='5') { 
        char* urlReal = (char*)malloc(sizeof(char)*200);
        strcpy(urlReal, "/picture.html");
        strncpy(m_realFile+len, urlReal, strlen(urlReal));
        free(urlReal);
    } else if(*(p+1)=='6') { 
        char* urlReal = (char*)malloc(sizeof(char)*200);
        strcpy(urlReal, "/video.html");
        strncpy(m_realFile+len, urlReal, strlen(urlReal));
        free(urlReal);
    } else if(*(p+1)=='7') { 
        char* urlReal = (char*)malloc(sizeof(char)*200);
        strcpy(urlReal, "/fans.html");
        strncpy(m_realFile+len, urlReal, strlen(urlReal));
        free(urlReal);
    } else {
        strncpy(m_realFile+len, m_url, FILENAME_LEN-len-1);
    }

    // 使用 stat() 函数获取文件的属性信息,存储在 m_file_stat 结构体中。
    // 如果 stat() 函数返回值小于 0,表示文件不存在,返回 NO_RESOURCE 错误。
    if(stat(m_realFile, &m_fileStat)<0) {
        return NO_RESOURCE;
    }
    // 检查文件的权限位 st_mode,看是否其他用户具有读取权限。
    // 如果没有读取权限,返回 FORBIDDEN_REQUEST 错误。
    if(!(m_fileStat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }
    //检查文件是否是目录
    if(S_ISDIR(m_fileStat.st_mode)) {
        return BAD_REQUEST;
    }
    //使用open()函数以只读模式打开文件
    //获取文件描述符
    int fd = open(m_realFile, O_RDONLY);
    // 使用 mmap() 函数将文件映射到内存中,并返回 FILE_REQUEST 表示处理成功
        // 使用 mmap() 函数将文件映射到内存中,返回映射的内存地址 m_file_address。
        // 0 表示自动选择映射地址, m_file_stat.st_size 是文件大小。
        // PROT_READ 表示只读映射, MAP_PRIVATE 表示创建私有映射。
        // fd 是文件描述符, 0 表示映射从文件开头开始。
    m_fileAddress = (char* )mmap(0, m_fileStat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

//获取一行情求文本
char* HttpCoon::getLine() {
    return m_readBuf + m_startLine;
}

//从状态机，用于分析出一行内容,在 HTTP 协议中,一个完密码整的行由两个字符组成: '\r' (回车) 和 '\n' (换行)。
//返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
HttpCoon::LINE_STATUS HttpCoon::parseLine() {
    char temp;
    for(; m_checkedIdx<m_readIdx; m_checkedIdx++) {
        temp = m_readBuf[m_checkedIdx];
        if(temp=='\r') {
            if((m_checkedIdx+1)==m_readIdx) {
                return LINE_OPEN;
            } else if(m_readBuf[m_checkedIdx+1]=='\n') {
                m_readBuf[m_checkedIdx++] = '\0';
                m_readBuf[m_checkedIdx++] = '\0';
                return LINE_OK;
            } else {
                return LINE_BAD;
            }
        } else if(temp=='\n') {
            if(m_checkedIdx>0 && m_readBuf[m_checkedIdx-1]=='\r') { //todo:大于0还是1
                m_readBuf[m_checkedIdx-1] = '\0';
                m_readBuf[m_checkedIdx++] = '\0';
                return LINE_OK;
            } else {
                return LINE_BAD;
            }
        } else {
            continue;//其他情况不处理
        }         
    }
    return LINE_OPEN;
}

void HttpCoon::unmap() {
    if(m_fileAddress) {
        munmap(m_fileAddress, m_fileStat.st_size);
        m_fileAddress = nullptr;
    }
}

//可变参数函数允许你编写一个函数，该函数可以接受不同数量和类型的参数，而不需要为每种可能的参数组合定义多个函数。
// 如: addResponse("%s %d %s\r\n", "HTTP/1.1", status, title);
    // format 参数是 "%s %d %s\r\n"。
    // argList 是可变参数列表,包含 "HTTP/1.1", status, title 三个参数。
//写到writeBuf中, 发送给sockfd的写缓冲区
bool HttpCoon::addResponse(const char* format, ...) { //...表示可变参数
    // 首先检查 m_writeIdx 是否超过了写缓冲区的大小 WRITE_BUFFER_SIZE。如果超过了,说明写缓冲区已满,无法添加更多的响应数据,因此返回 false。
    if(m_writeIdx>=WRITE_BUFFER_SIZE)
        return false;
    
    va_list argList;
    // 使用 va_start() 宏初始化一个 va_list 类型的变量 argList,用于获取可变参数。
    va_start(argList, format);
/*     使用 vsnprintf() 函数格式化可变参数,并将其写入到 m_writeBuf 缓冲区中。vsnprintf() 函数的参数包括:
        要写入的缓冲区指针 (m_writeBuf+m_writeIdx)
        剩余可用空间大小 (WRITE_BUFFER_SIZE-1-m_writeIdx)
        格式化字符串 (format)
        可变参数列表 (argList) */
    int len = vsnprintf(m_writeBuf+m_writeIdx, WRITE_BUFFER_SIZE-1-m_writeIdx, format, argList);
    // 如果 vsnprintf() 函数返回的长度大于等于剩余可用空间,说明缓冲区已满,无法添加更多的响应数据,因此调用 va_end() 宏释放可变参数列表,并返回 false。
    if(len>=(WRITE_BUFFER_SIZE-1-m_writeIdx)) {
        va_end(argList);
        return false;
    }
    // 如果 vsnprintf() 函数成功添加了响应数据,更新 m_writeIdx 的值,表示写缓冲区的写指针向后移动了 len 个字节。然后调用 va_end() 宏释放可变参数列表。
    m_writeIdx += len;
    va_end(argList);

    // LOG_INFO("Request: %s", m_writeBuf); todo

    // 最后,函数返回 true,表示响应数据已经成功添加到写缓冲区中。
    return true;
}

bool HttpCoon::addStatusLine(int status, const char* title) {
    return addResponse("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool HttpCoon::addHeaders(int contentLength) {
    return addContentLength(contentLength) && addLinger() && addBlankLine();
}

bool HttpCoon::addContent(const char* content) {
    return addResponse("s", content);
}

bool HttpCoon::addContentType() {
    return addResponse("Content-Type: %s\r\n", "text/html");
}

bool HttpCoon::addContentLength(int contentLength) {
    return addResponse("Content-Length: %d\r\n", contentLength);
}

bool HttpCoon::addLinger() {
    return addResponse("Connection:%s\r\n",(m_linger==true)?"keep-alive":"close");
}

bool HttpCoon::addBlankLine() {
    return addResponse("%s", "\r\n");
}