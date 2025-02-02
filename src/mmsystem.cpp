#include <windows.h>
#include <mmsystem.h>
#include <time.h>
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