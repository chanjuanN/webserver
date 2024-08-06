#ifndef CONFIG_H
#define CONFIG_H

#include "webserver.h"

using namespace std;

class Config {
public:
    Config();
    ~Config();

    void parseArg(int argc, char* argv[]);

public:
    int port;//端口号
    int asyncLogWrite;//日志写入方式：同步/异步
    int trigMode;//触发组合模式：LT/ET
    int listenTrigMode; //listenfd触发模式
    int connTrigMode;//connfd触发模式
    int optLinger;//是否优雅关闭连接
    int sqlNum;//数据库连接池数量
    int threadNum;//线程池线程数量
    int closeLog;//是否关闭日志
    int concurrencyModel;//并发模式选择：reactor/proactor
};

#endif