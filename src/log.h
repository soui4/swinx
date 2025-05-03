#ifndef _LOG_H_
#define _LOG_H_

#include <string>
#include <sstream>
#include <strapi.h>
#include <logdef.h>

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

        SLogStream &writeFormat(const char *format, ...);
        SLogStream &writeFormat(const wchar_t *format, ...);    
    private:
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
    public:
        Log(const char *tag, int level,const char * filename, const char *funname,int lineIndex);

        ~Log();
        SLogStream& stream();
        static void setLogCallback(::SWinxLogCallback logCallback);
        static void PrintLog(const char *log, int level);
        static void setLogLevel(int level);
    private:
        char m_tag[100];
        char m_logbuf[MAX_LOGLEN + 100];
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
#define LOG_FMT(tag,level, logformat, ...) \
    LOG_STM(tag, level).writeFormat(logformat, ##__VA_ARGS__)

#define LOG_STMD(tag) LOG_STM(tag,SLOG_DEBUG)
#define LOG_STMI(tag) LOG_STM(tag,SLOG_INFO)
#define LOG_STMW(tag) LOG_STM(tag,SLOG_WARN)
#define LOG_STME(tag) LOG_STM(tag,SLOG_ERROR)
#define LOG_STMF(tag) LOG_STM(tag,SLOG_FATAL)

#define LOG_FMTD(tag,logformat, ...) LOG_FMT(tag,SLOG_DEBUG,logformat,##__VA_ARGS__)
#define LOG_FMTI(tag,logformat, ...) LOG_FMT(tag,SLOG_INFO,logformat,##__VA_ARGS__)
#define LOG_FMTW(tag,logformat, ...) LOG_FMT(tag,SLOG_WARN,logformat,##__VA_ARGS__)
#define LOG_FMTE(tag,logformat, ...) LOG_FMT(tag,SLOG_ERROR,logformat,##__VA_ARGS__)
#define LOG_FMTF(tag,logformat, ...) LOG_FMT(tag,SLOG_FATAL,logformat,##__VA_ARGS__)

#define SLOG_STMD() LOG_STM(kLogTag,SLOG_DEBUG)
#define SLOG_STMI() LOG_STM(kLogTag,SLOG_INFO)
#define SLOG_STMW() LOG_STM(kLogTag,SLOG_WARN)
#define SLOG_STME() LOG_STM(kLogTag,SLOG_ERROR)
#define SLOG_STMF() LOG_STM(kLogTag,SLOG_FATAL)

#define SLOG_FMTD(logformat, ...) LOG_FMT(kLogTag,SLOG_DEBUG,logformat,##__VA_ARGS__)
#define SLOG_FMTI(logformat, ...) LOG_FMT(kLogTag,SLOG_INFO,logformat,##__VA_ARGS__)
#define SLOG_FMTW(logformat, ...) LOG_FMT(kLogTag,SLOG_WARN,logformat,##__VA_ARGS__)
#define SLOG_FMTE(logformat, ...) LOG_FMT(kLogTag,SLOG_ERROR,logformat,##__VA_ARGS__)
#define SLOG_FMTF(logformat, ...) LOG_FMT(kLogTag,SLOG_FATAL,logformat,##__VA_ARGS__)


#endif//_LOG_H_