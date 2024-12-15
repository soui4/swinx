#include <windows.h>
#include "log.h"
#include <assert.h>
#include <cstdarg>


#ifndef E_RANGE
#define E_RANGE 9944
#endif
namespace swinx
{
	/////////////////////////////////////////////////////////////////////////

    SLogStream::SLogStream(SLogStream&)
    {
    }

    SLogStream::SLogStream()
    {
    }

    SLogStream& SLogStream::writeWString(const wchar_t* t, int nLen)
    {
        DWORD dwLen = WideCharToMultiByte(CP_ACP, 0, t, nLen, NULL, 0, NULL, NULL);
        if (dwLen < Log::MAX_LOGLEN)
        {
            char buf[Log::MAX_LOGLEN];
            dwLen = WideCharToMultiByte(CP_ACP, 0, t, nLen, buf, dwLen, NULL, NULL);
            if (dwLen > 0)
            {
                buf[dwLen] = 0;
                writeData("%s", buf);
            }
        }
        return *this;
    }

    SLogStream& SLogStream::writeString(const char* t)
    {
        writeData("%s", t);
        return *this;
    }

    SLogStream& SLogStream::writeBinary(const SLogBinary& t)
    {
        writeData("%s", "\r\n\t[");
        for (int i = 0; i < t._len; i++)
        {
            if (i % 16 == 0)
            {
                writeData("%s", "\r\n\t");
                *this << (void*)(t._buf + i);
                writeData("%s", ": ");
            }
            writeData("%02x ", (unsigned char)t._buf[i]);
        }
        writeData("%s", "\r\n\t]\r\n\t");
        return *this;
    }

    SLogStream::SLogStream(char* buf, int len)
    {
        _begin = buf;
        _end = buf + len;
        _cur = _begin;
        buf[0] = 0;
    }

    SLogStream& SLogStream::writeLongLong(long long t)
    {
#if defined(WIN32) || defined(_WIN64)
        writeData("%I64d", t);
#else
        writeData("%lld", t);
#endif
        return *this;
    }

    SLogStream& SLogStream::writeULongLong(unsigned long long t)
    {
#if defined(WIN32) || defined(_WIN64)
        writeData("%I64u", t);
#else
        writeData("%llu", t);
#endif
        return *this;
    }

    SLogStream& SLogStream::writePointer(const void* t)
    {
        writeData("%p", t);
        return *this;
    }

    SLogStream& SLogStream::writeData(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        if (_cur < _end)
        {
            int len = 0;
            int count = (int)(_end - _cur) - 1;
#if defined(WIN32) || defined(_WIN64)
            len = _vsnprintf(_cur, count, fmt, args);
            if (len == count || (len == -1 && errno == E_RANGE))
            {
                len = count;
                *(_end - 1) = '\0';
            }
            else if (len < 0)
            {
                *_cur = '\0';
                len = 0;
            }
#else
            len = vsnprintf(_cur, count, fmt, args);
            if (len < 0)
            {
                *_cur = '\0';
                len = 0;
            }
            else if (len >= count)
            {
                len = count;
                *(_end - 1) = '\0';
            }
#endif
            _cur += len;
        }
        va_end(args);

        return *this;
    }

    SLogStream& SLogStream::operator<<(const SLogBinary& binary)
    {
        return writeBinary(binary);
    }

    SLogStream& SLogStream::operator<<(double t)
    {
        return writeData("%.4lf", t);
    }

    SLogStream& SLogStream::operator<<(float t)
    {
        return writeData("%.4f", t);
    }

    SLogStream& SLogStream::operator<<(unsigned long long t)
    {
        return writeULongLong(t);
    }

    SLogStream& SLogStream::operator<<(long long t)
    {
        return writeLongLong(t);
    }

    SLogStream& SLogStream::operator<<(unsigned long t)
    {
        return writeULongLong(t);
    }

    SLogStream& SLogStream::operator<<(long t)
    {
        return writeLongLong(t);
    }

    SLogStream& SLogStream::operator<<(unsigned int t)
    {
        return writeData("%u", t);
    }

    SLogStream& SLogStream::operator<<(int t)
    {
        return writeData("%d", t);
    }

    SLogStream& SLogStream::operator<<(unsigned short t)
    {
        return writeData("%u", (unsigned int)t);
    }

    SLogStream& SLogStream::operator<<(short t)
    {
        return writeData("%d", (int)t);
    }

    SLogStream& SLogStream::operator<<(unsigned char t)
    {
        return writeData("%u", (unsigned int)t);
    }

    SLogStream& SLogStream::operator<<(char t)
    {
        return writeData("%c", t);
    }

    SLogStream& SLogStream::operator<<(wchar_t t)
    {
        return writeWString(&t, 1);
    }

    SLogStream& SLogStream::operator<<(bool t)
    {
        return (t ? writeData("%s", "true") : writeData("%s", "false"));
    }

    SLogStream& SLogStream::operator<<(const wchar_t* t)
    {
        return writeWString(t);
    }

    SLogStream& SLogStream::operator<<(const char* t)
    {
        return writeString(t);
    }

    SLogStream& SLogStream::operator<<(const void* t)
    {
        return writePointer(t);
    }

    ////////////////////////////////////////////////////////////////////////
    static SWinxLogCallback gs_LogFunc = NULL;
    static int gs_level = Log::LOG_INFO;

	Log::Log(const char *tag, int level, const char* filename, const char* funname, int lineIndex):
        m_stream(m_logbuf,MAX_LOGLEN + 100)
    {
		assert(strlen(tag)<MAX_TAGLEN);
		strcpy(m_tag,tag);
		m_level = level;
#if defined(ENABLE_DEBUG)||defined(_DEBUG)
		m_lineInfo << funname << ":" <<filename<<":"<< lineIndex;
#else
		m_lineInfo << filename<<":"<< lineIndex;
#endif
	}

	Log::~Log() {
        tid_t tid = GetCurrentThreadId();
		if (gs_LogFunc != NULL)
		{
            const char * kLevelStr[]={
                    "unknown","default","verbose","debug","info","warn","error","fatal"
            };
			char buf[MAX_LOGLEN] = {0};
            const char * levelStr = (m_level>=LOG_UNKNOWN && m_level<=LOG_FATAL)?kLevelStr[m_level]:"invalid";
#ifdef NDEBUG
            snprintf(buf, sizeof(buf), "tid=%ld,%s,%s,%s", tid,m_tag, levelStr, m_logbuf);
#else
			snprintf(buf, sizeof(buf), "tid=%ld,%s,%s,%s,%s", tid,m_tag,levelStr, m_logbuf,m_lineInfo.str().c_str());
#endif
			gs_LogFunc(buf,m_level);
		} else if(m_level >= gs_level)
		{
            char buf[MAX_LOGLEN] = { 0 };
            snprintf(buf, sizeof(buf), "tid=%ld,%s,%d,%s,%s\n", tid,m_tag, m_level, m_logbuf, m_lineInfo.str().c_str());
            OutputDebugStringA(buf);
        }
	}

    SLogStream&Log::stream() {
		return m_stream;
	}

	void Log::PrintLog(const char *log, int level)
	{
		if (gs_LogFunc != NULL)
		{
			gs_LogFunc(log,level);
		} else{
#if defined(WEBRTC_ANDROID)
			__android_log_write(level, "yyaudio", log);
#elif defined (WEBRTC_WIN)
			OutputDebugStringA(log);
			OutputDebugStringA("\n");
#elif defined (WEBRTC_IOS) || defined (WEBRTC_MAC)
			char buf[MAX_LOGLEN] = {0};
			printf("%s\n", log);
        	fflush(stdout);
#endif
		}
	}
	void Log::setLogCallback(::SWinxLogCallback logCallback) {
		if (gs_LogFunc == NULL && logCallback != NULL)
		{
			gs_LogFunc = logCallback;
		}
	}

    void Log::setLogLevel(int level){

    }

}

std::stringstream& operator <<(std::stringstream& dst, const wchar_t* src) {
	char szTmp[1000];
	if (WideCharToMultiByte(0, 0, src, -1, szTmp, 1000, NULL, NULL)) {
		dst << szTmp;
	}
	return dst;
}