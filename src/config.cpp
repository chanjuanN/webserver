#include "config.h"

Config::Config() {
    port = 9006;//端口号默认9006
    asyncLogWrite = 0;//日志写入方式，默认同步
    trigMode = 0;//默认listenfd LT,connfd LT
    listenTrigMode = 0;//默认LT
    connTrigMode = 0;//默认LT
    optLinger = 0;//默认不使用优雅关闭连接
    sqlNum = 8;//默认为8
    threadNum = 8;//默认为8
    closeLog = 0;//默认不关闭
    concurrencyModel = 0;//默认为同步io模拟的proactor
}

Config::~Config() {}

void Config::parseArg(int argc, char* argv[]) {
    int opt;
    const char* str = "p:l:m:o:s:t:c:a:";
    while((opt=getopt(argc, argv, str))!=-1) {
        switch(opt) {
            case 'p': {
                port = atoi(optarg);
                break;
            }
            case 'l': {
                asyncLogWrite = atoi(optarg);
                break;
            }
            case 'm': {
                trigMode = atoi(optarg);
                break;
            }
            case 'o': {
                optLinger = atoi(optarg);
                break;
            }
            case 's': {
                sqlNum = atoi(optarg);
                break;
            }
            case 't': {
                threadNum = atoi(optarg);
                break;
            }
            case 'c': {
                closeLog = atoi(optarg);
                break;
            }
            case 'a': {
                concurrencyModel = atoi(optarg);
                break;
            }
            default:
                break;
        }
    }
}