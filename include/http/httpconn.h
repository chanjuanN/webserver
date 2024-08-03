/* http连接处理类----任务类
    通过主从状态机封装了http请求类。其中，主状态机在内部调用从状态机，从状态机将处理状态和数据传给主状态机。
    1. 客户端发出http连接请求；
    2. 从状态机读取数据，更新自身状态和接受数据，传给主状态机
    3. 主状态机根据从状态机状态，更新自身状态，决定响应请求还是继续读取数据
 */

#ifndef HTTPCOON_H
#define HTTPCOON_H

#include <netinet/in.h>
#include <string>
#include <string.h>//这个头文件是 C 标准库的一部分，但在 C++ 中也可以使用。它主要提供了一组用于处理 C 风格字符串（即以 null 结尾的字符数组）的函数。
#include <sys/stat.h>
#include <map>
#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <errno.h>
#include "locker.h"
#include <mysql/mysql.h> //todo 引用我的连接池,连接池中包含这个头文件
#include <sys/mman.h>
#include <sys/uio.h>

//todo很多还没添加,用的时候添加
// #include <sys/types.h>

// #include <arpa/inet.h>

using namespace std;

class HttpCoon {
public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    
    //http请求方法，目前只处理GET和POST
    enum METHOD {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };

    //http响应状态?? todo
    enum HTTP_CODE {
        NO_REQUEST = 0,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };

    //主状态机
    enum CHECK_STATE {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };

    //从状态机
    enum LINE_STATUS {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    HttpCoon();
    ~HttpCoon();

    void init(int sockfd, const sockaddr_in &addr, char* root, int TrigMode, int closeLog, string user, string passwd, string sqlname);
    void closeConn(bool realClose = true);//缺省参数赋值尽量写在声明中,不要写在定义中
    void process();//处理请求
    bool readOnce();
    bool write();
    sockaddr_in* getAddress();//获取ip地址
    void initMysqlResult();//todo

private:
    void init();
    HTTP_CODE processRead();
    bool processWrite(HTTP_CODE ret);
    HTTP_CODE parseRequestLine(char* text);
    HTTP_CODE parseHeaders(char* text);
    HTTP_CODE parseContent(char* text);
    HTTP_CODE doRequest();
    char* getLine();
    LINE_STATUS parseLine();
    void unmap();
    //可变参数函数允许你编写一个函数，该函数可以接受不同数量和类型的参数，而不需要为每种可能的参数组合定义多个函数。
    bool addResponse(const char* format, ...);//...表示可变参数
    bool addContent(const char* content);
    bool addStatusLine(int status, const char* title);
    bool addHeaders(int contentLength);
    bool addContentType();
    bool addContentLength(int contentLength);
    bool addLinger();
    bool addBlankLine();

public:
    int m_timerFlag;//处理定时器任务
    int m_improv;//reactor模式下工作线程已经做完了io操作
    static int m_epollfd;
    static int m_userCount;
    MYSQL* m_mysql;//todo
    int m_reactorState;//读为0,写为1

private:
    int m_sockfd;
    sockaddr_in m_address;
    char m_readBuf[READ_BUFFER_SIZE];
    long m_readIdx;
    long m_checkedIdx;
    int m_startLine;
    char m_writeBuf[WRITE_BUFFER_SIZE];
    int m_writeIdx;
    CHECK_STATE m_checkState;
    METHOD m_method;
    char m_realFile[FILENAME_LEN];//记录请求的文件地址
    char* m_url;
    char* m_version;
    char* m_host;
    long m_contentLength;
    bool m_linger;
    char* m_fileAddress;//将文件映射到内存中,映射的内存地址
    struct stat m_fileStat;//文件状态
    struct iovec m_iv[2];
    int m_ivCount;
    // int m_cgi;//是否启用POST,不用cgi了,todo
    char* m_string;//存储请求体数据
    int m_bytesToSend;
    int m_bytesHaveSend;
    char* m_docRoot;//所有资源的根目录

    map<string, string> m_users;//存储数据库中记录的用户信息
    Locker m_lock;//保护m_users;
    int m_TrigMode;//LT/ET
    int m_closeLog;

    char m_sqlUser[100];
    char m_sqlPasswd[100];
    char m_sqlName[100];
};


#endif