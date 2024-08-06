#include "config.h"
#include <iostream>

int main(int argc, char* argv[]) {
    //修改数据库信息
    string user = "root";
    string passwd = "123456ncj";
    string databaseName = "ncjdb";

    //命令行解析
    Config config;
    config.parseArg(argc, argv);

    //服务器
    WebServer server;
    //初始化
    server.init(config.port, user, passwd, databaseName, config.asyncLogWrite, 
            config.optLinger, config.trigMode, config.sqlNum, config.threadNum, 
            config.closeLog, config.concurrencyModel);

    server.logWrite();//日志
    server.sqlPool();//数据库
    server.threadPool();//线程池
    server.trigMode();//触发模式
    server.eventListen(); //监听
  
    server.eventLoop();//运行

    return 0;
}