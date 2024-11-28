#ifndef _DEBUG_H_
#define _DEBUG_H_

#include "log.h"

#define DBG_LOG SLOG_FMTI

#define WARN SLOG_FMTW
#define TRACE SLOG_FMTD
#define ERR SLOG_FMTE
#define FIXME SLOG_FMTD

static inline char* wine_dbgstr_rect(const RECT * rect) {
    static char szBuf[100];
    sprintf(szBuf,"(%d,%d)-(%d,%d)", rect->left, rect->top,
        rect->right, rect->bottom);
    return szBuf;
}
#endif //_DEBUG_H_