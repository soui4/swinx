#ifndef __WININET_H_
#define __WININET_H_

/*
 * wininet.h — minimal WinINet surface for the swinx Win32 bridge.
 *
 * WebCore's clipboard/drag code includes <wininet.h> only for the URL length
 * limits (WCHAR urlBuffer[INTERNET_MAX_URL_LENGTH] when reading/writing a
 * .url InternetShortcut file), so that is all this header provides today.
 * There is no WinINet runtime behind it: the swinx hosts that need network
 * access (wke) use libcurl, exactly like the Windows Cairo/Curl build does.
 *
 * The values mirror the Windows SDK (wininet.h); INTERNET_MAX_URL_LENGTH is
 * the SDK's own formula rather than a literal so the components stay in sync.
 */

#include <windows.h>

#define INTERNET_MAX_HOST_NAME_LENGTH 256
#define INTERNET_MAX_USER_NAME_LENGTH 128
#define INTERNET_MAX_PASSWORD_LENGTH 128
#define INTERNET_MAX_PORT_NUMBER_LENGTH 5
#define INTERNET_MAX_PORT_NUMBER_VALUE 65535
#define INTERNET_MAX_PATH_LENGTH 2048
#define INTERNET_MAX_SCHEME_LENGTH 32

#define INTERNET_MAX_URL_LENGTH                                                \
    (INTERNET_MAX_SCHEME_LENGTH + sizeof("://") + INTERNET_MAX_HOST_NAME_LENGTH \
     + sizeof(":") + INTERNET_MAX_PORT_NUMBER_LENGTH + sizeof("/")              \
     + INTERNET_MAX_PATH_LENGTH)

#endif // __WININET_H_
