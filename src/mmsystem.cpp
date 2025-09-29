#include <windows.h>
#include <mmsystem.h>
#include <time.h>

#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <unistd.h>
#include <map>
#elif defined(__linux__)
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "log.h"
#define kLogTag "mmsystem"

DWORD timeGetTime(void)
{
    DWORD uptime = 0;
    struct timespec on;
    if (clock_gettime(CLOCK_MONOTONIC, &on) == 0)
        uptime = on.tv_sec * 1000 + on.tv_nsec / 1000000;
    return uptime;
}
#define MMSYSTIME_MININTERVAL (1)
#define MMSYSTIME_MAXINTERVAL (65535)

MMRESULT timeGetDevCaps(LPTIMECAPS lpCaps, UINT wSize)
{
    if (lpCaps == 0)
    {
        SLOG_STMW() << "invalid lpCaps";
        return TIMERR_NOCANDO;
    }

    if (wSize < sizeof(TIMECAPS))
    {
        SLOG_STMW() << "invalid wSize";
        return TIMERR_NOCANDO;
    }

    lpCaps->wPeriodMin = MMSYSTIME_MININTERVAL;
    lpCaps->wPeriodMax = MMSYSTIME_MAXINTERVAL;
    return TIMERR_NOERROR;
}

MMRESULT timeBeginPeriod(UINT wPeriod)
{
    if (wPeriod < MMSYSTIME_MININTERVAL || wPeriod > MMSYSTIME_MAXINTERVAL)
        return TIMERR_NOCANDO;

    if (wPeriod > MMSYSTIME_MININTERVAL)
    {
        SLOG_STMW() << "Stub; we set our timer resolution at minimum";
    }

    return 0;
}

MMRESULT timeEndPeriod(UINT wPeriod)
{
    if (wPeriod < MMSYSTIME_MININTERVAL || wPeriod > MMSYSTIME_MAXINTERVAL)
        return TIMERR_NOCANDO;

    if (wPeriod > MMSYSTIME_MININTERVAL)
    {
        SLOG_STMW() << "Stub; we set our timer resolution at minimum";
    }
    return 0;
}

#ifdef __APPLE__
class AudioPlayer {
  private:
    SystemSoundID m_soundID;
    AudioPlayer()
        : m_soundID(0)
    {
    }

  public:
    BOOL play(LPCSTR pszSound, BOOL bPurge)
    {
        BOOL ret = FALSE;
        do
        {
            CFStringRef soundPath = CFStringCreateWithCString(NULL, pszSound, kCFStringEncodingUTF8);
            if (soundPath == NULL)
            {
                break;
            }

            CFURLRef soundURL = CFURLCreateWithFileSystemPath(NULL, soundPath, kCFURLPOSIXPathStyle, false);
            if (soundURL == NULL)
            {
                CFRelease(soundPath);
                break;
            }
            if (bPurge && m_soundID)
            {
                AudioServicesDisposeSystemSoundID(m_soundID);
            }
            m_soundID = 0;
            OSStatus status = AudioServicesCreateSystemSoundID(soundURL, &m_soundID);
            if (status == noErr)
            {
                AudioServicesPlaySystemSound(m_soundID);
                ret = TRUE;
            }
            CFRelease(soundURL);
            CFRelease(soundPath);
        } while (false);
        return ret;
    }
    static AudioPlayer *getInstance()
    {
        static AudioPlayer instance;
        return &instance;
    }
};
#elif defined(__linux__)
class AudioPlayer {
  private:
    pid_t m_soundID;
    AudioPlayer()
        : m_soundID(0)
    {
    }

  public:
    BOOL play(LPCSTR pszSound, BOOL bPurge)
    {
        if (bPurge || pszSound == NULL || pszSound[0] == '\0')
        {
            if (m_soundID != 0)
            {
                kill(m_soundID, SIGTERM);
                waitpid(m_soundID, NULL, WNOHANG);
                m_soundID = 0;
            }
            if (pszSound == NULL || pszSound[0] == '\0')
                return TRUE;
        }

        pid_t pid = fork();
        if (pid < 0)
        {
            return FALSE;
        }
        if (pid == 0)
        {
            execlp("aplay", "aplay", pszSound, (char *)NULL);
            _exit(127);
        }
        m_soundID = pid;
        return TRUE;
    }
    static AudioPlayer *getInstance()
    {
        static AudioPlayer instance;
        return &instance;
    }
};
#endif
BOOL PlaySound(LPCSTR pszSound, HMODULE hmod, DWORD fdwSound)
{
    if (fdwSound & (SND_MEMORY | SND_RESOURCE | SND_ALIAS | SND_ALIAS_ID))
    {
        return FALSE;
    }
#if defined(__APPLE__) || defined(__linux__)
    return AudioPlayer::getInstance()->play(pszSound, fdwSound & SND_PURGE);
#else
    // For other platforms, return FALSE as before
    return FALSE;
#endif
}
