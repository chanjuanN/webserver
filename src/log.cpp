#include "log.h"
#include "block_queue.h"

//C++11以后,使用局部变量懒汉不用加锁
Log* Log::getInstance() {
    static Log instance;
    return &instance;
}

//写日志线程一直在异步写日志
void* Log::flushLogThread(void* args) {
    Log::getInstance()->asyncWriteLog();
}

bool Log::init(const char* fileName, int closeLog, int logBufSize, int splitLines, int maxQueueSize) {
    if(maxQueueSize>0) {
        m_isAsync = true;//异步日志设置阻塞队列长度
        /*
            生产者线程(业务线程)只需要将日志消息推入队列,不需要关心日志写入的具体过程。
            消费者线程(日志写入线程)则负责异步地从队列中取出日志消息并写入磁盘。
            这种生产者-消费者模式降低了组件之间的耦合度。
        */
        m_logQueue = new BlockQueue<string>(maxQueueSize);//阻塞队列用于存储产生的日志消息，等待日志写入线程取出写入磁盘
        pthread_t tid;
        pthread_create(&tid, nullptr, flushLogThread, nullptr);//flush_log_thread为回调函数,这里表示创建线程异步写日志
    }
    m_closeLog = closeLog;
    m_logBufSize = logBufSize;
    m_splitLines = splitLines;
    m_buf = new char[m_logBufSize];
    memset(m_buf, '\0', m_logBufSize);

    time_t t = time(nullptr);//time() 函数返回当前时间,以秒为单位,从 1970年1月1日 UTC 开始计算。
    struct tm* sysTm = localtime(&t);//struct tm 结构体包含年、月、日、时、分、秒等字段,表示本地时间。
    struct tm myTm = *sysTm;

    const char* p = strrchr(fileName, '/');//在字符串 file_name 中查找最后一次出现字符 '/' 的位置
    char logFullName[256]={0};
    if(p==nullptr) { //没有’/‘，说明只有名字，即相对路径
        // struct tm 中的年份是从 1900 年开始计算的,因此需要加 1900。同样,月份是从 0 开始计算的,因此需要加 1
        snprintf(logFullName, 255, "%d_%02d_%02d_%s", myTm.tm_year+1900, myTm.tm_mon+1, myTm.tm_mday, fileName);
    } else { //将最后一个‘/’前后的内容分开，名字加上日期，filename = ‘./Serverlog'
        strcpy(m_logName, p+1);//logName = "Serverlog"
        strncpy(m_dirName, fileName, p-fileName+1);//dirname = "./"
        snprintf(logFullName, 255, "%d_%02d_%02d_%s", myTm.tm_year+1900, myTm.tm_mon+1, myTm.tm_mday, m_logName);
    }
    m_today = myTm.tm_mday;
    m_fp = fopen(logFullName, "a");//追加模式,如果文件不存在则创建新文件,如果文件存在则在文件末尾追加内容
    if(m_fp==nullptr) {
        return false;
    } 
    return true;
}

void Log::writeLog(int level, const char* format, ...) {
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);
    time_t t  = now.tv_sec;
    struct tm* sysTm = localtime(&t);
    struct tm myTm = *sysTm;

    char s[16] = {0};
    switch(level) {
        case 0: {
            strcpy(s, "[debug]:");
            break;
        }
        case 1: {
            strcpy(s, "[info]:");
            break;
        }
        case 2: {
            strcpy(s, "[warn]:");
            break;
        }
        case 3: {
            strcpy(s, "[error]:");
            break;
        }
        default: {
            strcpy(s, "[info]:");
            break;
        }            
    }

    m_mutex.lock();
    m_count++;
    if(m_today!=myTm.tm_mday || m_count%m_splitLines==0) { //everyday log 
        char newLog[256] = {0};
        //在使用文件 I/O 的过程中,操作系统通常会对数据进行缓存,以提高读写效率。当我们调用 fwrite() 或 fprintf() 等函数写入数据时,
        // 数据并不会立即写入磁盘,而是先存储在内存缓冲区中。只有当缓冲区满或者调用 fflush() 函数时,数据才会真正写入磁盘。
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};
        snprintf(tail, 16, "%d_%02d_%02d_", myTm.tm_year+1900, myTm.tm_mon+1, myTm.tm_mday);
        
        if(m_today!=myTm.tm_mday) {
            //新的一天
            snprintf(newLog, 255, "%s%s%s", m_dirName, tail, m_logName);
            m_today = myTm.tm_mday;
            m_count = 0;
        } else {
            //最大行数超过了，新建一个文件，追加序号区分
            snprintf(newLog, 255, "%s%s%s.%lld", m_dirName, tail, m_logName, m_count/m_splitLines);
        }
        m_fp = fopen(newLog, "a");
    }
    m_mutex.unlock();

    va_list valst;
    va_start(valst, format);
    string logStr;
    m_mutex.lock();
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                myTm.tm_year+1900, myTm.tm_mon+1, myTm.tm_mday,
                myTm.tm_hour, myTm.tm_min, myTm.tm_sec, now.tv_usec, s);
    int m = vsnprintf(m_buf+n, m_logBufSize-n-1, format, valst);
    m_buf[n+m] = '\n';
    m_buf[n+m+1] = '\0';
    logStr = m_buf;
    m_mutex.unlock();

    if(m_isAsync && !m_logQueue->isFull()) {
        //异步，放入阻塞队列中
        m_logQueue->push(logStr);
    } else {
        //同步
        m_mutex.lock();
        fputs(logStr.c_str(), m_fp);//直接写到日志文件中
        m_mutex.unlock();
    }
    va_end(valst);//使用 va_end 函数清理 va_list 变量 valst。
}

void Log::flush(void) {
    m_mutex.lock();
    fflush(m_fp);//强制刷新写入流缓冲区
    m_mutex.unlock();
}


Log::Log() {
    m_count = 0;
    m_isAsync = false;//默认同步
}

Log::~Log() {
    if(!m_fp) {
        fclose(m_fp);
    }
}

void* Log::asyncWriteLog() {
    //从阻塞队列取出一条日志，写入文件
    string logstr;
    while(m_logQueue->pop(logstr)) {
        m_mutex.lock();
        fputs(logstr.c_str(), m_fp);
        m_mutex.unlock();
    }
}