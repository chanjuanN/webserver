/* 同步/异步日志系统
===============
同步/异步日志系统主要涉及了两个模块，一个是日志模块，一个是阻塞队列模块,其中加入阻塞队列模块主要是解决异步写入日志做准备.
> * 自定义阻塞队列
> * 单例模式创建日志
> * 同步日志
> * 异步日志
> * 实现按天、超行分类 */

#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <string>
#include <string.h>
#include "locker.h"
#include <stdarg.h>
#include <pthread.h>
#include <time.h>
#include "block_queue.h"

using namespace std;

class Log {
public:
    static Log* getInstance();
    static void* flushLogThread(void* args);
    bool init(const char* fileName, int closeLog, int logBufSize=8192, int splitLines=5000000, int maxQueueSize=0);
    void writeLog(int level, const char* format, ...);
    void flush(void);

private:
    Log();
    virtual ~Log();
    void* asyncWriteLog();

private:
    char m_dirName[128];
    char m_logName[128];
    int m_splitLines;
    int m_logBufSize;
    long long m_count;
    int m_today;
    FILE* m_fp;
    char* m_buf;
    BlockQueue<string>* m_logQueue;
    bool m_isAsync;
    Locker m_mutex;
    int m_closeLog;    
};

//todo，这里编译报错了，改了
#define LOG_DEBUG(format, ...) if(0==m_closeLog) {Log::getInstance()->writeLog(0, format, __VA_ARGS__); Log::getInstance()->flush();}
#define LOG_INFO(format, ...) if(0==m_closeLog) {Log::getInstance()->writeLog(1, format, __VA_ARGS__); Log::getInstance()->flush();}
#define LOG_WARN(format, ...) if(0==m_closeLog) {Log::getInstance()->writeLog(2, format, __VA_ARGS__); Log::getInstance()->flush();}
#define LOG_ERROR(format, ...) if(0==m_closeLog) {Log::getInstance()->writeLog(3, format, __VA_ARGS__); Log::getInstance()->flush();}

#endif