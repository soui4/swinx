#ifndef _LOG_H_
#define _LOG_H_

#include <string>
#include <sstream>
#include <strapi.h>
typedef void (*SWinxLogCallback)(const char* pLogStr, int level);

namespace swinx
{
    class SLogBinary {
    public:
        SLogBinary(const char* buf, int len)
        {
            _buf = buf;
            _len = len;
        }
        const char* _buf;
        int _len;
    };

    class SLogStream {
    public:
        SLogStream(char* buf, int len);

    public:
        SLogStream& operator<<(const void* t);
        SLogStream& operator<<(const char* t);
        SLogStream& operator<<(const wchar_t* t);
        SLogStream& operator<<(bool t);
        SLogStream& operator<<(char t);
        SLogStream& operator<<(wchar_t t);
        SLogStream& operator<<(unsigned char t);
        SLogStream& operator<<(short t);
        SLogStream& operator<<(unsigned short t);
        SLogStream& operator<<(int t);
        SLogStream& operator<<(unsigned int t);
        SLogStream& operator<<(long t);
        SLogStream& operator<<(unsigned long t);
        SLogStream& operator<<(long long t);
        SLogStream& operator<<(unsigned long long t);
        SLogStream& operator<<(float t);
        SLogStream& operator<<(double t);
        SLogStream& operator<<(const SLogBinary& binary);

    private:
        SLogStream& writeData(const char* ft, ...);
        SLogStream& writeLongLong(long long t);
        SLogStream& writeULongLong(unsigned long long t);
        SLogStream& writePointer(const void* t);
        SLogStream& writeString(const char* t);
        SLogStream& writeWString(const wchar_t* t, int nLen = -1);
        SLogStream& writeBinary(const SLogBinary& t);

    private:
        SLogStream();
        SLogStream(SLogStream&);
        char* _begin;
        char* _end;
        char* _cur;
    };

    class Log
    {
    public:
        enum{
            MAX_TAGLEN = 100,
            MAX_LOGLEN = 1024,
        };
        enum LogPriority {
            /** For internal use only.  */
            LOG_UNKNOWN = 0,
            /** The default priority, for internal use only.  */
            LOG_DEFAULT, /* only for SetMinPriority() */
            /** Verbose logging. Should typically be disabled for a release apk. */
            LOG_VERBOSE,
            /** Debug logging. Should typically be disabled for a release apk. */
            LOG_DEBUG,
            /** Informational logging. Should typically be disabled for a release apk. */
            LOG_INFO,
            /** Warning logging. For use with recoverable failures. */
            LOG_WARN,
            /** Error logging. For use with unrecoverable failures. */
            LOG_ERROR,
            /** Fatal logging. For use when aborting. */
            LOG_FATAL,
        };
    public:
        Log(const char *tag, int level,const char * filename, const char *funname,int lineIndex);

        ~Log();
        SLogStream& stream();
        static void setLogCallback(::SWinxLogCallback logCallback);
        static void PrintLog(const char *log, int level);
        static void setLogLevel(int level);
    private:
        char m_tag[100]{ 0 };
        char m_logbuf[MAX_LOGLEN + 100]{ 0 };
        int   m_level;
        SLogStream m_stream;
        std::stringstream m_lineInfo;
    };
}

std::stringstream& operator <<(std::stringstream& dst, const wchar_t* src);

#ifndef __PRETTY_FUNCTION__
#define __PRETTY_FUNCTION__ __FUNCTION__
#endif//__PRETTY_FUNCTION__

#define LOG_STM(tag,level) swinx::Log(tag,level,__FILE__,__PRETTY_FUNCTION__,__LINE__).stream()
#define LOG_FMT(tag,level, logformat, ...)                                     \
    do                                                                      \
    {                                                                       \
        if(sizeof(logformat[0])==sizeof(char)){                             \
            char logbuf[swinx::Log::MAX_LOGLEN] = { 0 };                  \
            int nLen = snprintf(logbuf, swinx::Log::MAX_LOGLEN,           \
                (const char *)logformat, ##__VA_ARGS__);                    \
                LOG_STM(tag, level) << logbuf;                                 \
        }                                                                   \
        else                                                                \
        {                                                                   \
            wchar_t logbuf[swinx::Log::MAX_LOGLEN] = { 0 };               \
            int nLen = _snwprintf(logbuf, swinx::Log::MAX_LOGLEN,         \
                (const wchar_t *)logformat, ##__VA_ARGS__);                 \
            LOG_STM(tag, level) << logbuf;                                     \
        }                                                                   \
    }while(false);

#define LOG_STMD(tag) LOG_STM(tag,swinx::Log::LOG_DEBUG)
#define LOG_STMI(tag) LOG_STM(tag,swinx::Log::LOG_INFO)
#define LOG_STMW(tag) LOG_STM(tag,swinx::Log::LOG_WARN)
#define LOG_STME(tag) LOG_STM(tag,swinx::Log::LOG_ERROR)
#define LOG_STMF(tag) LOG_STM(tag,swinx::Log::LOG_FATAL)

#define LOG_FMTD(tag,logformat, ...) LOG_FMT(tag,swinx::Log::LOG_DEBUG,logformat,##__VA_ARGS__)
#define LOG_FMTI(tag,logformat, ...) LOG_FMT(tag,swinx::Log::LOG_INFO,logformat,##__VA_ARGS__)
#define LOG_FMTW(tag,logformat, ...) LOG_FMT(tag,swinx::Log::LOG_WARN,logformat,##__VA_ARGS__)
#define LOG_FMTE(tag,logformat, ...) LOG_FMT(tag,swinx::Log::LOG_ERROR,logformat,##__VA_ARGS__)
#define LOG_FMTF(tag,logformat, ...) LOG_FMT(tag,swinx::Log::LOG_FATAL,logformat,##__VA_ARGS__)

#define SLOG_STMD() LOG_STM(kLogTag,swinx::Log::LOG_DEBUG)
#define SLOG_STMI() LOG_STM(kLogTag,swinx::Log::LOG_INFO)
#define SLOG_STMW() LOG_STM(kLogTag,swinx::Log::LOG_WARN)
#define SLOG_STME() LOG_STM(kLogTag,swinx::Log::LOG_ERROR)
#define SLOG_STMF() LOG_STM(kLogTag,swinx::Log::LOG_FATAL)

#define SLOG_FMTD(logformat, ...) LOG_FMT(kLogTag,swinx::Log::LOG_DEBUG,logformat,##__VA_ARGS__)
#define SLOG_FMTI(logformat, ...) LOG_FMT(kLogTag,swinx::Log::LOG_INFO,logformat,##__VA_ARGS__)
#define SLOG_FMTW(logformat, ...) LOG_FMT(kLogTag,swinx::Log::LOG_WARN,logformat,##__VA_ARGS__)
#define SLOG_FMTE(logformat, ...) LOG_FMT(kLogTag,swinx::Log::LOG_ERROR,logformat,##__VA_ARGS__)
#define SLOG_FMTF(logformat, ...) LOG_FMT(kLogTag,swinx::Log::LOG_FATAL,logformat,##__VA_ARGS__)


#endif//_LOG_H_