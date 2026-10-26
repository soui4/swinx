#ifndef _SYS_API_H
#define _SYS_API_H
#include <sfr_config.h>
#include <ctypes.h>
#include <unistd.h>
#include <guiddef.h>
#include <stdio.h>
#include <dlfcn.h>
#include <strapi.h>
#ifdef __cplusplus
extern "C"
{
#endif //__cplusplus

    typedef struct RTL_CRITICAL_SECTION
    {
        void *pMutex;
    } RTL_CRITICAL_SECTION;

    typedef RTL_CRITICAL_SECTION CRITICAL_SECTION;
    typedef CRITICAL_SECTION *PCRITICAL_SECTION;
    typedef CRITICAL_SECTION *LPCRITICAL_SECTION;

    VOID WINAPI InitializeCriticalSection(LPCRITICAL_SECTION lpCriticalSection);

    VOID WINAPI EnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection);

    VOID WINAPI LeaveCriticalSection(LPCRITICAL_SECTION lpCriticalSection);

    BOOL WINAPI TryEnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection);

    VOID WINAPI DeleteCriticalSection(LPCRITICAL_SECTION lpCriticalSection);

    typedef struct _RTL_SRWLOCK
    {
        PVOID Ptr;
    } RTL_SRWLOCK, *PRTL_SRWLOCK;

    typedef RTL_SRWLOCK SRWLOCK, *PSRWLOCK;

    VOID WINAPI InitializeSRWLock(PSRWLOCK SRWLock);

    VOID WINAPI UninitializeSRWLock(PSRWLOCK SRWLock);

    VOID WINAPI AcquireSRWLockExclusive(PSRWLOCK SRWLock);

    VOID WINAPI ReleaseSRWLockExclusive(PSRWLOCK SRWLock);

    VOID WINAPI AcquireSRWLockShared(PSRWLOCK SRWLock);

    VOID WINAPI ReleaseSRWLockShared(PSRWLOCK SRWLock);

    BOOLEAN WINAPI TryAcquireSRWLockExclusive(PSRWLOCK SRWLock);

    BOOLEAN WINAPI TryAcquireSRWLockShared(PSRWLOCK SRWLock);

    // ---- One-time initialization (INIT_ONCE) ----
#define INIT_ONCE_CTX_RESERVED_BITS 2

    typedef struct _RTL_RUN_ONCE
    {
        int State;
        LPVOID Context;
    } RTL_RUN_ONCE, *PRTL_RUN_ONCE;

    typedef RTL_RUN_ONCE INIT_ONCE;
    typedef PRTL_RUN_ONCE PINIT_ONCE;
    typedef PRTL_RUN_ONCE LPINIT_ONCE;

#define INIT_ONCE_STATIC_INIT \
    {                         \
        0                     \
    }

#define RTL_RUN_ONCE_CHECK_ONLY  0x00000001
#define RTL_RUN_ONCE_ASYNC       0x00000002
#define RTL_RUN_ONCE_INIT_FAILED 0x00000004

#define INIT_ONCE_CHECK_ONLY  RTL_RUN_ONCE_CHECK_ONLY
#define INIT_ONCE_ASYNC       RTL_RUN_ONCE_ASYNC
#define INIT_ONCE_INIT_FAILED RTL_RUN_ONCE_INIT_FAILED

    typedef BOOL(WINAPI *PINIT_ONCE_FN)(PINIT_ONCE InitOnce, PVOID Parameter, PVOID *Context);

    VOID WINAPI InitOnceInitialize(PINIT_ONCE InitOnce);

    BOOL WINAPI InitOnceExecuteOnce(PINIT_ONCE InitOnce, PINIT_ONCE_FN InitFn, PVOID Parameter, LPVOID *Context);

    BOOL WINAPI InitOnceBeginInitialize(LPINIT_ONCE lpInitOnce, DWORD dwFlags, PBOOL fPending, LPVOID *lpContext);

    BOOL WINAPI InitOnceComplete(LPINIT_ONCE lpInitOnce, DWORD dwFlags, LPVOID lpContext);

    // ---- Thread Local Storage (TLS) ----
#ifndef TLS_MINIMUM_AVAILABLE
#define TLS_MINIMUM_AVAILABLE 64
#endif
#ifndef TLS_OUT_OF_INDEXES
#define TLS_OUT_OF_INDEXES ((DWORD)0xFFFFFFFF)
#endif

    DWORD WINAPI TlsAlloc(VOID);

    BOOL WINAPI TlsFree(DWORD dwTlsIndex);

    LPVOID WINAPI TlsGetValue(DWORD dwTlsIndex);

    BOOL WINAPI TlsSetValue(DWORD dwTlsIndex, LPVOID lpTlsValue);

    LONG WINAPI InterlockedDecrement(LONG volatile *v);

    LONG WINAPI InterlockedIncrement(LONG volatile *v);

    LONG WINAPI InterlockedCompareExchange(LONG volatile *v, LONG Exchange, LONG Comparand);

    LONG WINAPI InterlockedExchange(LONG volatile *Target, LONG Value);

    LONG WINAPI InterlockedExchangeAdd(LONG volatile *v, LONG Increment);

    int64_t WINAPI InterlockedDecrement64(int64_t volatile *v);

    int64_t WINAPI InterlockedIncrement64(int64_t volatile *v);

    int64_t WINAPI InterlockedCompareExchange64(int64_t volatile *v, int64_t Exchange, int64_t Comparand);

    int64_t WINAPI InterlockedExchangeAdd64(int64_t volatile *v, int64_t Increment);

    void qsort_s(void *_Base, size_t _NumOfElements, size_t _SizeOfElements, int(__cdecl *_PtFuncCompare)(void *, const void *, const void *), void *_Context);

    BOOL IIDFromString(LPCWSTR s, GUID *id);

    void SetLastError(int e);

    int GetLastError();

    /* 格式化错误信息。swinx 只支持 FORMAT_MESSAGE_FROM_SYSTEM（消息号按
     * errno 解释），其余标志被忽略；ALLOCATE_BUFFER 时用 LocalAlloc 返回
     * 缓冲区，调用方须用 LocalFree 释放（与 Win32 一致）。 */
#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100
#define FORMAT_MESSAGE_IGNORE_INSERTS  0x00000200
#define FORMAT_MESSAGE_FROM_STRING     0x00000400
#define FORMAT_MESSAGE_FROM_HMODULE    0x00000800
#define FORMAT_MESSAGE_FROM_SYSTEM     0x00001000
#define FORMAT_MESSAGE_ARGUMENT_ARRAY  0x00002000
#define FORMAT_MESSAGE_MAX_WIDTH_MASK  0x000000FF
    DWORD WINAPI FormatMessageA(DWORD dwFlags, LPCVOID lpSource, DWORD dwMessageId, DWORD dwLanguageId,
                                LPSTR lpBuffer, DWORD nSize, va_list *Arguments);
    DWORD WINAPI FormatMessageW(DWORD dwFlags, LPCVOID lpSource, DWORD dwMessageId, DWORD dwLanguageId,
                                LPWSTR lpBuffer, DWORD nSize, va_list *Arguments);
#ifdef UNICODE
#define FormatMessage FormatMessageW
#else
#define FormatMessage FormatMessageA
#endif // UNICODE

    int MulDiv(int a, int b, int c);

    tid_t WINAPI GetCurrentThreadId();

    /* Win32 里 GetCurrentThread 不是函数而是伪句柄宏（-2 = 当前线程），
     * 取它永远成功且不需要关闭。swinx 沿用这个约定，需要真实线程信息的
     * API（GetThreadTimes 等）认这个伪句柄。 */
#define GetCurrentThread() ((HANDLE)(LONG_PTR)-2)

    BOOL WINAPI GetThreadTimes(HANDLE hThread, LPFILETIME lpCreationTime, LPFILETIME lpExitTime,
                               LPFILETIME lpKernelTime, LPFILETIME lpUserTime);

    int MultiByteToWideChar(int cp, int flags, const char *src, int len, wchar_t *dst, int dstLen);

    int WideCharToMultiByte(int cp, int flags, const wchar_t *src, int len, char *dst, int dstLen, LPCSTR p1, BOOL *p2);

#define STIF_DEFAULT     0x00000000L
#define STIF_SUPPORT_HEX 0x00000001L
    BOOL StrToInt64ExW(const wchar_t *str, DWORD flags, LONGLONG *ret);
    BOOL StrToIntExW(const wchar_t *str, DWORD flags, INT *ret);

    BOOL StrToInt64ExA(const char *str, DWORD flags, LONGLONG *ret);
    BOOL StrToIntExA(const char *str, DWORD flags, INT *ret);

#ifdef UNICODE
#define StrToInt64Ex StrToInt64ExW
#define StrToIntEx   StrToIntExW
#else
#define StrToInt64Ex StrToInt64ExA
#define StrToIntEx   StrToIntExA
#endif // UNICODE

    typedef struct _SECURITY_ATTRIBUTES
    {
        DWORD nLength;
        LPVOID lpSecurityDescriptor;
        BOOL bInheritHandle;
    } SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

#define STARTF_USESHOWWINDOW    0x00000001
#define STARTF_USESIZE          0x00000002
#define STARTF_USEPOSITION      0x00000004
#define STARTF_USECOUNTCHARS    0x00000008
#define STARTF_USEFILLATTRIBUTE 0x00000010
#define STARTF_RUNFULLSCREEN    0x00000020 // ignored for non-x86 platforms
#define STARTF_FORCEONFEEDBACK  0x00000040
#define STARTF_FORCEOFFFEEDBACK 0x00000080
#define STARTF_USESTDHANDLES    0x00000100

    typedef struct _STARTUPINFOA
    {
        DWORD cb;
        LPSTR lpReserved;
        LPSTR lpDesktop;
        LPSTR lpTitle;
        DWORD dwX;
        DWORD dwY;
        DWORD dwXSize;
        DWORD dwYSize;
        DWORD dwXCountChars;
        DWORD dwYCountChars;
        DWORD dwFillAttribute;
        DWORD dwFlags;
        WORD wShowWindow;
        WORD cbReserved2;
        LPBYTE lpReserved2;
        HANDLE hStdInput;
        HANDLE hStdOutput;
        HANDLE hStdError;
    } STARTUPINFOA, *LPSTARTUPINFOA;

    typedef struct _STARTUPINFOW
    {
        DWORD cb;
        LPWSTR lpReserved;
        LPWSTR lpDesktop;
        LPWSTR lpTitle;
        DWORD dwX;
        DWORD dwY;
        DWORD dwXSize;
        DWORD dwYSize;
        DWORD dwXCountChars;
        DWORD dwYCountChars;
        DWORD dwFillAttribute;
        DWORD dwFlags;
        WORD wShowWindow;
        WORD cbReserved2;
        LPBYTE lpReserved2;
        HANDLE hStdInput;
        HANDLE hStdOutput;
        HANDLE hStdError;
    } STARTUPINFOW, *LPSTARTUPINFOW;

    typedef struct _PROCESS_INFORMATION
    {
        HANDLE hProcess;
        HANDLE hThread;
        DWORD dwProcessId;
        tid_t dwThreadId;
    } PROCESS_INFORMATION, *LPPROCESS_INFORMATION;

    enum
    {
        Verb_Unknown = 0,
        Verb_Open,
        Verb_RunAs, // root user
    };

#define __in
#define __out
    BOOL WINAPI CreateProcessAsUserA(__in HANDLE hToken, __in LPCSTR lpApplicationName, __in LPSTR lpCommandLine, __in LPSECURITY_ATTRIBUTES lpProcessAttributes, __in LPSECURITY_ATTRIBUTES lpThreadAttributes, __in BOOL bInheritHandles, __in DWORD dwCreationFlags, __in LPVOID lpEnvironment, __in LPCSTR lpCurrentDirectory, __in LPSTARTUPINFOA lpStartupInfo, __out LPPROCESS_INFORMATION lpProcessInformation);

    BOOL WINAPI CreateProcessAsUserW(__in HANDLE hToken, __in LPCWSTR lpApplicationName, __in LPWSTR lpCommandLine, __in LPSECURITY_ATTRIBUTES lpProcessAttributes, __in LPSECURITY_ATTRIBUTES lpThreadAttributes, __in BOOL bInheritHandles, __in DWORD dwCreationFlags, __in LPVOID lpEnvironment, __in LPCWSTR lpCurrentDirectory, __in LPSTARTUPINFOW lpStartupInfo, __out LPPROCESS_INFORMATION lpProcessInformation);

    BOOL WINAPI CreateProcessA(__in LPCSTR lpApplicationName, __in LPSTR lpCommandLine, __in LPSECURITY_ATTRIBUTES lpProcessAttributes, __in LPSECURITY_ATTRIBUTES lpThreadAttributes, __in BOOL bInheritHandles, __in DWORD dwCreationFlags, __in LPVOID lpEnvironment, __in LPCSTR lpCurrentDirectory, __in LPSTARTUPINFOA lpStartupInfo, __out LPPROCESS_INFORMATION lpProcessInformation);
    BOOL WINAPI CreateProcessW(__in LPCWSTR lpApplicationName, __in LPWSTR lpCommandLine, __in LPSECURITY_ATTRIBUTES lpProcessAttributes, __in LPSECURITY_ATTRIBUTES lpThreadAttributes, __in BOOL bInheritHandles, __in DWORD dwCreationFlags, __in LPVOID lpEnvironment, __in LPCWSTR lpCurrentDirectory, __in LPSTARTUPINFOW lpStartupInfo, __out LPPROCESS_INFORMATION lpProcessInformation);

#undef __in
#undef __out

#ifdef UNICODE
#define STARTUPINFO         STARTUPINFOW
#define CreateProcessAsUser CreateProcessAsUserW
#define CreateProcess       CreateProcessW
#else
#define STARTUPINFO         STARTUPINFOA
#define CreateProcessAsUser CreateProcessAsUserA
#define CreateProcess       CreateProcessA
#endif // UNICODE

    BOOL WINAPI GetExitCodeProcess(HANDLE hProcess, LPDWORD lpExitCode);

    // 读取指定PID的进程状态文件，获取其有效用户ID, 0-root, -1-failed.
    int WINAPI get_process_uid(int pid);

    DWORD WINAPI GetCurrentProcessId();

    DWORD WINAPI GetProcessId(HANDLE Process);

    HANDLE WINAPI GetCurrentProcess_Priv(void);
#define GetCurrentProcess GetCurrentProcess_Priv
    void WINAPI GetLocalTime(SYSTEMTIME *pSysTime);
    void WINAPI GetSystemTime(SYSTEMTIME *lpSystemTime);
    BOOL WINAPI LocalFileTimeToFileTime(const FILETIME *lpLocalFileTime, LPFILETIME lpFileTime);
    BOOL WINAPI FileTimeToLocalFileTime(const FILETIME *lpFileTime, LPFILETIME lpLocalFileTime);

    time_t _mkgmtime(struct tm *_Tm);
    int _localtime64_s(struct tm *ptm, const __time64_t *ptime);

    void PostThreadMessageA(uint64_t tid, UINT msg, WPARAM wp, LPARAM lp);
    void PostThreadMessageW(uint64_t tid, UINT msg, WPARAM wp, LPARAM lp);
#ifdef UNICODE
#define PostThreadMessage PostThreadMessageW
#else
#define PostThreadMessage PostThreadMessageA
#endif // UNICODE

    BOOL WaitMessage();
    BOOL GetMessage(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax);
    BOOL PeekMessage(LPMSG pMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);

    BOOL TranslateMessage(const LPMSG pMsg);
    BOOL DispatchMessage(const LPMSG pMsg);

    /* Win32 里 GetMessage / PeekMessage / DispatchMessage 都有 A/W 两份名字，
     * 但它们的参数（MSG*）本身不做 A/W 区分。swinx 因此只保留了一份「无后缀」
     * 的统一实现——这里补出 A/W 后缀名，使得直接书写 GetMessageW、PeekMessageA、
     * DispatchMessageW 等后缀名的代码（例如 UNICODE 构建下的 wkex）也能解析到
     * 同一份实现，不必再写一遍。 */
#define GetMessageA      GetMessage
#define GetMessageW      GetMessage
#define PeekMessageA     PeekMessage
#define PeekMessageW     PeekMessage
#define DispatchMessageA DispatchMessage
#define DispatchMessageW DispatchMessage

    /* 消息队列状态。返回值高字为 QS_* 状态位，低字为队列中未处理消息数
     * （Win32 语义），由各平台 SConnection::GetQueueStatus 提供。 */
    DWORD WINAPI GetQueueStatus(UINT flags);

    typedef struct tagLASTINPUTINFO
    {
        UINT cbSize;
        DWORD dwTime;
    } LASTINPUTINFO, *PLASTINPUTINFO;

    /* 最近一次键盘/鼠标输入的时间戳（GetTickCount 口径，ms）。
     * swinx 在消息泵取出输入类消息时刷新该时间戳。 */
    BOOL WINAPI GetLastInputInfo(PLASTINPUTINFO plii);

    int GetSystemScale();
    int GetSystemMetrics(int nIndex);

    HCURSOR SetCursor(HCURSOR hCursor);
    HCURSOR GetCursor(VOID);

    SHORT GetKeyState(int nVirtKey);

    BOOL CallMsgFilter(LPMSG lpMsg, int nCode);

#if !SFR_HOSTED
    // host: the MinGW CRT declares these with __time64_t / int return types
    __time64_t _mktime64(const struct tm *ptime);

    __time64_t _time64(__time64_t *_Time);
#endif

    HCURSOR LoadCursorA(HINSTANCE hInstance, LPCSTR lpCursorName);
    HCURSOR LoadCursorW(HINSTANCE hInstance, LPCWSTR lpCursorName);
#ifdef UNICODE
#define LoadCursor LoadCursorW
#else
#define LoadCursor LoadCursorA
#endif // UNICODE

    BOOL DestroyCursor(HCURSOR hCursor);

    BOOL WINAPI IsBadReadPtr(const void *ptr, size_t size);
    BOOL WINAPI IsBadWritePtr(const void *ptr, size_t size);

    BOOL WINAPI IsBadStringPtrA(LPCSTR lpsz, UINT_PTR ucchMax);

    BOOL WINAPI IsBadStringPtrW(LPCWSTR lpsz, UINT_PTR ucchMax);
#ifdef UNICODE
#define IsBadStringPtr IsBadStringPtrW
#else
#define IsBadStringPtr IsBadStringPtrA
#endif // UNICODE

    FILE *_wfopen(const wchar_t *path, const wchar_t *mode);

#define HEAP_NO_SERIALIZE             0x00000001
#define HEAP_GROWABLE                 0x00000002
#define HEAP_GENERATE_EXCEPTIONS      0x00000004
#define HEAP_ZERO_MEMORY              0x00000008
#define HEAP_REALLOC_IN_PLACE_ONLY    0x00000010
#define HEAP_TAIL_CHECKING_ENABLED    0x00000020
#define HEAP_FREE_CHECKING_ENABLED    0x00000040
#define HEAP_DISABLE_COALESCE_ON_FREE 0x00000080
#define HEAP_CREATE_ALIGN_16          0x00010000
#define HEAP_CREATE_ENABLE_TRACING    0x00020000
#define HEAP_CREATE_ENABLE_EXECUTE    0x00040000
#define HEAP_MAXIMUM_TAG              0x0FFF
#define HEAP_PSEUDO_TAG_FLAG          0x8000
#define HEAP_TAG_SHIFT                18
#define HEAP_CREATE_SEGMENT_HEAP      0x00000100
#define HEAP_CREATE_HARDENED          0x00000200

#define GMEM_FIXED          0x0000
#define GMEM_MOVEABLE       0x0002
#define GMEM_NOCOMPACT      0x0010
#define GMEM_NODISCARD      0x0020
#define GMEM_ZEROINIT       0x0040
#define GMEM_MODIFY         0x0080
#define GMEM_DISCARDABLE    0x0100
#define GMEM_NOT_BANKED     0x1000
#define GMEM_SHARE          0x2000
#define GMEM_DDESHARE       0x2000
#define GMEM_NOTIFY         0x4000
#define GMEM_LOWER          GMEM_NOT_BANKED
#define GMEM_DISCARDED      0x4000
#define GMEM_LOCKCOUNT      0x00ff
#define GMEM_INVALID_HANDLE 0x8000

#define GHND (GMEM_MOVEABLE | GMEM_ZEROINIT)
#define GPTR (GMEM_FIXED | GMEM_ZEROINIT)

#define LMEM_FIXED          0
#define LMEM_MOVEABLE       0x0002
#define LMEM_NOCOMPACT      0x0010
#define LMEM_NODISCARD      0x0020
#define LMEM_ZEROINIT       0x0040
#define LMEM_MODIFY         0x0080
#define LMEM_DISCARDABLE    0x0F00
#define LMEM_DISCARDED      0x4000
#define LMEM_INVALID_HANDLE 0x8000
#define LMEM_LOCKCOUNT      0x00FF

#define LPTR (LMEM_FIXED | LMEM_ZEROINIT)
#define LHND (LMEM_MOVEABLE | LMEM_ZEROINIT)

    HANDLE WINAPI GetProcessHeap();

    HANDLE WINAPI HeapCreate(DWORD flOptions, size_t dwInitialSize, size_t dwMaximumSize);

    BOOL WINAPI HeapDestroy(HANDLE hHeap);

    BOOL WINAPI HeapLock(HANDLE hHeap);

    BOOL WINAPI HeapUnlock(HANDLE hHeap);

    LPVOID
    WINAPI
    HeapAlloc(HANDLE hHeap, DWORD dwFlags, size_t dwBytes);

    BOOL WINAPI HeapFree(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem);

    LPVOID WINAPI HeapReAlloc(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem, SIZE_T dwBytes);

    SIZE_T WINAPI HeapSize(HANDLE hHeap, DWORD dwFlags, LPCVOID lpMem);

    BOOL WINAPI HeapValidate(HANDLE hHeap, DWORD dwFlags, LPCVOID lpMem);

    BOOL WINAPI FlushInstructionCache(HANDLE hProcess, LPCVOID lpBaseAddress, size_t dwSize);

    // Virtual memory functions
    LPVOID WINAPI VirtualAlloc(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect);
    BOOL WINAPI VirtualFree(LPVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType);

    typedef struct _MEMORY_BASIC_INFORMATION
    {
        PVOID BaseAddress;
        PVOID AllocationBase;
        DWORD AllocationProtect;
        SIZE_T RegionSize;
        DWORD State;
        DWORD Protect;
        DWORD Type;
    } MEMORY_BASIC_INFORMATION, *PMEMORY_BASIC_INFORMATION;

    BOOL WINAPI VirtualProtect(LPVOID lpAddress, SIZE_T dwSize, DWORD flNewProtect, LPDWORD lpflOldProtect);
    SIZE_T WINAPI VirtualQuery(LPCVOID lpAddress, PMEMORY_BASIC_INFORMATION lpBuffer, SIZE_T dwLength);

    // Timer related functions
    typedef VOID(CALLBACK *PTIMERAPCROUTINE)(LPVOID lpArgToCompletionRoutine, DWORD dwTimerLowValue, DWORD dwTimerHighValue);
    typedef VOID(CALLBACK *WAITORTIMERCALLBACK)(PVOID lpParameter, BOOLEAN TimerOrWaitFired);

    HANDLE WINAPI CreateWaitableTimerA(LPSECURITY_ATTRIBUTES lpTimerAttributes, BOOL bManualReset, LPCSTR lpTimerName);
    HANDLE WINAPI CreateWaitableTimerW(LPSECURITY_ATTRIBUTES lpTimerAttributes, BOOL bManualReset, LPCWSTR lpTimerName);

#ifdef UNICODE
#define CreateWaitableTimer CreateWaitableTimerW
#else
#define CreateWaitableTimer CreateWaitableTimerA
#endif // UNICODE

    BOOL WINAPI SetWaitableTimer(HANDLE hTimer, const LARGE_INTEGER *lpDueTime, LONG lPeriod, PTIMERAPCROUTINE lpCompletionRoutine, LPVOID lpArgToCompletionRoutine, BOOL fResume);
    BOOL WINAPI CancelWaitableTimer(HANDLE hTimer);
    HANDLE WINAPI OpenWaitableTimerA(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCSTR lpTimerName);
    HANDLE WINAPI OpenWaitableTimerW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpTimerName);

#ifdef UNICODE
#define OpenWaitableTimer OpenWaitableTimerW
#else
#define OpenWaitableTimer OpenWaitableTimerA
#endif // UNICODE

    // Timer Queue related functions
    HANDLE WINAPI CreateTimerQueue(void);
    BOOL WINAPI CreateTimerQueueTimer(PHANDLE phNewTimer, HANDLE hTimerQueue, WAITORTIMERCALLBACK Callback, PVOID Parameter, DWORD DueTime, DWORD Period, ULONG Flags);
    BOOL WINAPI ChangeTimerQueueTimer(HANDLE hTimerQueue, HANDLE hTimer, DWORD DueTime, DWORD Period);
    BOOL WINAPI DeleteTimerQueueTimer(HANDLE hTimerQueue, HANDLE hTimer, HANDLE hCompletionEvent);
    BOOL WINAPI DeleteTimerQueue(HANDLE hTimerQueue);
    BOOL WINAPI DeleteTimerQueueEx(HANDLE hTimerQueue, HANDLE hCompletionEvent);

    HGLOBAL WINAPI GlobalAlloc(UINT flags, SIZE_T size);
    HGLOBAL WINAPI GlobalReAlloc(_In_ HGLOBAL hMem, _In_ SIZE_T dwBytes, _In_ UINT uFlags);
    HGLOBAL WINAPI GlobalFree(HLOCAL hmem);
    UINT WINAPI GlobalFlags(_In_ HGLOBAL hMem);
    HLOCAL WINAPI GlobalHandle(_In_ LPCVOID pMem);

    BOOL WINAPI GlobalUnlock(_In_ HGLOBAL hMem);

    LPVOID
    WINAPI
    GlobalLock(_In_ HGLOBAL hMem);

    SIZE_T
    WINAPI
    GlobalSize(_In_ HGLOBAL hMem);

    UINT WINAPI LocalSize(HLOCAL hMem);
    HLOCAL WINAPI LocalAlloc(UINT flags, SIZE_T size);
    HLOCAL WINAPI LocalReAlloc(HLOCAL hmem, SIZE_T size, UINT flags);
    HLOCAL WINAPI LocalFree(HLOCAL hmem);
    LPVOID WINAPI LocalLock(HLOCAL hmem);
    BOOL WINAPI LocalUnlock(HLOCAL hmem);
    UINT WINAPI LocalFlags(_In_ HLOCAL hMem);
    HLOCAL WINAPI LocalHandle(_In_ LPCVOID pMem);

    DWORD WINAPI GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize);
    DWORD WINAPI GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename, DWORD nSize);

#ifdef UNICODE
#define GetModuleFileName GetModuleFileNameW
#else
#define GetModuleFileName GetModuleFileNameA
#endif // UNICODE

    UINT WINAPI GetSystemDirectoryA(LPSTR lpBuffer, UINT uSize);
    UINT WINAPI GetSystemDirectoryW(LPWSTR lpBuffer, UINT uSize);

#ifdef UNICODE
#define GetSystemDirectory GetSystemDirectoryW
#else
#define GetSystemDirectory GetSystemDirectoryA
#endif // UNICODE

    DWORD WINAPI ExpandEnvironmentStringsA(LPCSTR lpSrc, LPSTR lpDst, DWORD nSize);
    DWORD WINAPI ExpandEnvironmentStringsW(LPCWSTR lpSrc, LPWSTR lpDst, DWORD nSize);

#ifdef UNICODE
#define ExpandEnvironmentStrings ExpandEnvironmentStringsW
#else
#define ExpandEnvironmentStrings ExpandEnvironmentStringsA
#endif // UNICODE

#ifdef __x86_64__
    typedef INT_PTR(FAR WINAPI *FARPROC)();
    typedef INT_PTR(NEAR WINAPI *NEARPROC)();
    typedef INT_PTR(WINAPI *PROC)();
#else
typedef int(FAR WINAPI *FARPROC)();
typedef int(NEAR WINAPI *NEARPROC)();
typedef int(WINAPI *PROC)();
#endif // __x86_64__

    HMODULE WINAPI LoadLibraryA(LPCSTR lpFileName);

    HMODULE WINAPI LoadLibraryW(LPCWSTR lpFileName);

#define DONT_RESOLVE_DLL_REFERENCES       0x00000001
#define LOAD_LIBRARY_AS_DATAFILE          0x00000002
#define LOAD_WITH_ALTERED_SEARCH_PATH     0x00000008
#define LOAD_LIBRARY_AS_IMAGE_RESOURCE    0x00000020
#define LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE 0x00000040
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR  0x00000100
#define LOAD_LIBRARY_SEARCH_APPLICATION_DIR 0x00000200
#define LOAD_LIBRARY_SEARCH_USER_DIRS     0x00000400
#define LOAD_LIBRARY_SEARCH_SYSTEM32      0x00000800
#define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS  0x00001000

    HMODULE WINAPI LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
    HMODULE WINAPI LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);

    DWORD WINAPI GetDllDirectoryA(DWORD nBufferLength, LPSTR lpBuffer);
    DWORD WINAPI GetDllDirectoryW(DWORD nBufferLength, LPWSTR lpBuffer);
    BOOL WINAPI SetDllDirectoryA(LPCSTR lpPathName);
    BOOL WINAPI SetDllDirectoryW(LPCWSTR lpPathName);
#ifdef _UNICODE
#define LoadLibrary     LoadLibraryW
#define LoadLibraryEx   LoadLibraryExW
#define GetDllDirectory GetDllDirectoryW
#define SetDllDirectory SetDllDirectoryW
#else
#define LoadLibrary     LoadLibraryA
#define LoadLibraryEx   LoadLibraryExA
#define GetDllDirectory GetDllDirectoryA
#define SetDllDirectory SetDllDirectoryA
#endif

    BOOL WINAPI FreeLibrary(HMODULE hModule);

    FARPROC WINAPI GetProcAddress(HMODULE hModule, LPCSTR lpProcName);

    HANDLE WINAPI CreateSemaphoreA(LPSECURITY_ATTRIBUTES lpSemaphoreAttributes, LONG lInitialCount, LONG lMaximumCount, LPCSTR name);

    HANDLE WINAPI CreateSemaphoreW(LPSECURITY_ATTRIBUTES lpSemaphoreAttributes, LONG lInitialCount, LONG lMaximumCount, LPCWSTR lpName);

    HANDLE WINAPI OpenSemaphoreA(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCSTR lpName);

    HANDLE WINAPI OpenSemaphoreW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName);

    BOOL WINAPI ReleaseSemaphore(HANDLE hSemaphore, LONG lReleaseCount, LPLONG lpPreviousCount);

    HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState, LPCSTR lpName);

    HANDLE WINAPI CreateEventW(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState, LPCWSTR lpName);

    HANDLE WINAPI OpenEventA(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCSTR lpName);

    HANDLE WINAPI OpenEventW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName);

    HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCSTR lpName);

    HANDLE WINAPI CreateMutexW(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCWSTR lpName);

    BOOL WINAPI ReleaseMutex(HANDLE hMutex);

    HANDLE WINAPI OpenMutexA(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCSTR lpName);

    HANDLE WINAPI OpenMutexW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName);

#ifdef _UNICODE
#define CreateEvent     CreateEventW
#define OpenEvent       OpenEventW
#define CreateSemaphore CreateSemaphoreW
#define OpenSemaphore   OpenSemaphoreW
#define CreateMutex     CreateMutexW
#define OpenMutex       OpenMutexW
#else
#define CreateEvent     CreateEventA
#define OpenEvent       OpenEventA
#define CreateSemaphore CreateSemaphoreA
#define OpenSemaphore   OpenSemaphoreA
#define CreateMutex     CreateMutexA
#define OpenMutex       OpenMutexA
#endif

    BOOL WINAPI CloseHandle(HANDLE h);

    BOOL WINAPI ResetEvent(HANDLE hEvent);

    BOOL WINAPI SetEvent(HANDLE hEvent);

    DWORD WINAPI WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds);

    DWORD WINAPI WaitForSingleObjectEx(HANDLE hHandle, DWORD dwMilliseconds, BOOL bAlertable);

    /* -------------------------------------------------- 旧式线程池等待
     *
     * RegisterWaitForSingleObject 让句柄可等待，但回调不落在线程池线程上；
     * swinx 的实现是每个注册项一根等待线程（见 sysobjs.cpp）。标志位取值
     * 照 SDK 的 threadpoollegacyapiset.h，其中只有 WT_EXECUTEONLYONCE 会
     * 真正改变行为，其余是"放到哪类线程上执行"的调度提示 —— 单线程模型
     * 下没有区别，忽略它们不会改变可观察语义。 */
#define WT_EXECUTEDEFAULT               0x00000000
#define WT_EXECUTEINIOTHREAD            0x00000001
#define WT_EXECUTEINUITHREAD            0x00000002
#define WT_EXECUTEINWAITTHREAD          0x00000004
#define WT_EXECUTEONLYONCE              0x00000008
#define WT_EXECUTELONGFUNCTION          0x00000010
#define WT_EXECUTEINTIMERTHREAD         0x00000020
#define WT_EXECUTEINPERSISTENTIOTHREAD  0x00000040
#define WT_EXECUTEINPERSISTENTTHREAD    0x00000080
#define WT_TRANSFER_IMPERSONATION       0x00000100

    typedef VOID(CALLBACK *WAITORTIMERCALLBACK)(PVOID lpParameter, BOOLEAN TimerOrWaitFired);

    BOOL WINAPI RegisterWaitForSingleObject(PHANDLE phNewWaitObject, HANDLE hObject,
                                            WAITORTIMERCALLBACK Callback, PVOID Context,
                                            ULONG dwMilliseconds, ULONG dwFlags);

    BOOL WINAPI UnregisterWait(HANDLE WaitHandle);

    BOOL WINAPI UnregisterWaitEx(HANDLE WaitHandle, HANDLE CompletionEvent);

    DWORD WINAPI WaitForMultipleObjects(DWORD nCount, const HANDLE *lpHandles, BOOL bWaitAll, DWORD dwMilliseconds);

    DWORD WINAPI MsgWaitForMultipleObjects(DWORD nCount, const HANDLE *pHandles, BOOL fWaitAll, DWORD dwMilliseconds, DWORD dwWakeMask);

    BOOL WINAPI GetHandleName(HANDLE h, char szName[1001]);

    VOID WINAPI Sleep(DWORD dwMilliseconds);

    void WINAPI OutputDebugStringA(LPCSTR lpOutputString);

    void WINAPI OutputDebugStringW(LPCWSTR lpOutputString);

#ifdef _UNICODE
#define OutputDebugString OutputDebugStringW
#else
#define OutputDebugString OutputDebugStringA
#endif

#define EqualMemory(Destination, Source, Length) (!memcmp((Destination), (Source), (Length)))
#define MoveMemory(Destination, Source, Length)  memmove((Destination), (Source), (Length))
#define CopyMemory(Destination, Source, Length)  memcpy((Destination), (Source), (Length))
#define FillMemory(Destination, Length, Fill)    memset((Destination), (Fill), (Length))
#define ZeroMemory(Destination, Length)          memset((Destination), 0, (Length))

    void WINAPI set_error(int e);

#define SECTION_QUERY                0x0001
#define SECTION_MAP_WRITE            0x0002
#define SECTION_MAP_READ             0x0004
#define SECTION_MAP_EXECUTE          0x0008
#define SECTION_EXTEND_SIZE          0x0010
#define SECTION_MAP_EXECUTE_EXPLICIT 0x0020 // not included in SECTION_ALL_ACCESS

#define SECTION_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | SECTION_QUERY | SECTION_MAP_WRITE | SECTION_MAP_READ | SECTION_MAP_EXECUTE | SECTION_EXTEND_SIZE)

    // System information structures
    typedef struct _SYSTEM_INFO
    {
        union {
            DWORD dwOemId;
            struct
            {
                WORD wProcessorArchitecture;
                WORD wReserved;
            } DUMMYSTRUCTNAME;
        } DUMMYUNIONNAME;
        DWORD dwPageSize;
        LPVOID lpMinimumApplicationAddress;
        LPVOID lpMaximumApplicationAddress;
        DWORD_PTR dwActiveProcessorMask;
        DWORD dwNumberOfProcessors;
        DWORD dwProcessorType;
        DWORD dwAllocationGranularity;
        WORD wProcessorLevel;
        WORD wProcessorRevision;
    } SYSTEM_INFO, *LPSYSTEM_INFO;

// Processor architecture constants
#define PROCESSOR_ARCHITECTURE_INTEL          0
#define PROCESSOR_ARCHITECTURE_MIPS           1
#define PROCESSOR_ARCHITECTURE_ALPHA          2
#define PROCESSOR_ARCHITECTURE_PPC            3
#define PROCESSOR_ARCHITECTURE_SHX            4
#define PROCESSOR_ARCHITECTURE_ARM            5
#define PROCESSOR_ARCHITECTURE_IA64           6
#define PROCESSOR_ARCHITECTURE_ALPHA64        7
#define PROCESSOR_ARCHITECTURE_MSIL           8
#define PROCESSOR_ARCHITECTURE_AMD64          9
#define PROCESSOR_ARCHITECTURE_IA32_ON_WIN64  10
#define PROCESSOR_ARCHITECTURE_NEUTRAL        11
#define PROCESSOR_ARCHITECTURE_ARM64          12
#define PROCESSOR_ARCHITECTURE_ARM32_ON_WIN64 13
#define PROCESSOR_ARCHITECTURE_IA32_ON_ARM64  14
#define PROCESSOR_ARCHITECTURE_UNKNOWN        0xFFFF

    // OS version structures
    typedef struct _OSVERSIONINFOA
    {
        DWORD dwOSVersionInfoSize;
        DWORD dwMajorVersion;
        DWORD dwMinorVersion;
        DWORD dwBuildNumber;
        DWORD dwPlatformId;
        CHAR szCSDVersion[128]; // Maintenance string for PSS usage
    } OSVERSIONINFOA, *POSVERSIONINFOA, *LPOSVERSIONINFOA;

    typedef struct _OSVERSIONINFOW
    {
        DWORD dwOSVersionInfoSize;
        DWORD dwMajorVersion;
        DWORD dwMinorVersion;
        DWORD dwBuildNumber;
        DWORD dwPlatformId;
        WCHAR szCSDVersion[128]; // Maintenance string for PSS usage
    } OSVERSIONINFOW, *POSVERSIONINFOW, *LPOSVERSIONINFOW;
#ifdef UNICODE
    typedef OSVERSIONINFOW OSVERSIONINFO;
    typedef POSVERSIONINFOW POSVERSIONINFO;
    typedef LPOSVERSIONINFOW LPOSVERSIONINFO;
#else
typedef OSVERSIONINFOA OSVERSIONINFO;
typedef POSVERSIONINFOA POSVERSIONINFO;
typedef LPOSVERSIONINFOA LPOSVERSIONINFO;
#endif // UNICODE

    typedef struct _OSVERSIONINFOEXA
    {
        DWORD dwOSVersionInfoSize;
        DWORD dwMajorVersion;
        DWORD dwMinorVersion;
        DWORD dwBuildNumber;
        DWORD dwPlatformId;
        CHAR szCSDVersion[128]; // Maintenance string for PSS usage
        WORD wServicePackMajor;
        WORD wServicePackMinor;
        WORD wSuiteMask;
        BYTE wProductType;
        BYTE wReserved;
    } OSVERSIONINFOEXA, *POSVERSIONINFOEXA, *LPOSVERSIONINFOEXA;
    typedef struct _OSVERSIONINFOEXW
    {
        DWORD dwOSVersionInfoSize;
        DWORD dwMajorVersion;
        DWORD dwMinorVersion;
        DWORD dwBuildNumber;
        DWORD dwPlatformId;
        WCHAR szCSDVersion[128]; // Maintenance string for PSS usage
        WORD wServicePackMajor;
        WORD wServicePackMinor;
        WORD wSuiteMask;
        BYTE wProductType;
        BYTE wReserved;
    } OSVERSIONINFOEXW, *POSVERSIONINFOEXW, *LPOSVERSIONINFOEXW;
#ifdef UNICODE
    typedef OSVERSIONINFOEXW OSVERSIONINFOEX;
    typedef POSVERSIONINFOEXW POSVERSIONINFOEX;
    typedef LPOSVERSIONINFOEXW LPOSVERSIONINFOEX;
#else
typedef OSVERSIONINFOEXA OSVERSIONINFOEX;
typedef POSVERSIONINFOEXA POSVERSIONINFOEX;
typedef LPOSVERSIONINFOEXA LPOSVERSIONINFOEX;
#endif // UNICODE

// Platform ID constants
#define VER_PLATFORM_WIN32s        0
#define VER_PLATFORM_WIN32_WINDOWS 1
#define VER_PLATFORM_WIN32_NT      2

// OSVERSIONINFOEX.wProductType values
#define VER_NT_WORKSTATION       0x0000001
#define VER_NT_DOMAIN_CONTROLLER 0x0000002
#define VER_NT_SERVER            0x0000003

// Virtual memory constants
#define MEM_COMMIT                  0x1000
#define MEM_RESERVE                 0x2000
#define MEM_DECOMMIT                0x4000
#define MEM_RELEASE                 0x8000
#define MEM_FREE                    0x10000
#define MEM_PRIVATE                 0x20000
#define MEM_MAPPED                  0x40000
#define MEM_IMAGE                   0x1000000
#define MEM_RESET                   0x80000
#define MEM_TOP_DOWN                0x100000
#define MEM_WRITE_WATCH             0x200000
#define MEM_PHYSICAL                0x400000
#define MEM_ROTATE                  0x800000
#define MEM_DIFFERENT_IMAGE_BASE_OK 0x800000
#define MEM_COALESCE_PLACEHOLDERS   0x1000000
#define MEM_PRESERVE_PLACEHOLDER    0x2000000
#define MEM_RESERVE_PLACEHOLDER     0x4000000

#define PAGE_NOACCESS          0x01
#define PAGE_READONLY          0x02
#define PAGE_READWRITE         0x04
#define PAGE_WRITECOPY         0x08
#define PAGE_EXECUTE           0x10
#define PAGE_EXECUTE_READ      0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_EXECUTE_WRITECOPY 0x80
#define PAGE_GUARD             0x100
#define PAGE_NOCACHE           0x200
#define PAGE_WRITECOMBINE      0x400

    // System information functions
    VOID WINAPI GetSystemInfo(LPSYSTEM_INFO lpSystemInfo);
    BOOL WINAPI GetVersionExA(LPOSVERSIONINFOA lpVersionInformation);
    BOOL WINAPI GetVersionExW(LPOSVERSIONINFOW lpVersionInformation);

#ifdef UNICODE
#define GetVersionEx GetVersionExW
#else
#define GetVersionEx GetVersionExA
#endif // UNICODE

    // Computer and user name functions
    BOOL WINAPI GetComputerNameA(LPSTR lpBuffer, LPDWORD lpnSize);
    BOOL WINAPI GetComputerNameW(LPWSTR lpBuffer, LPDWORD lpnSize);

#ifdef UNICODE
#define GetComputerName GetComputerNameW
#else
#define GetComputerName GetComputerNameA
#endif // UNICODE

    BOOL WINAPI GetUserNameA(LPSTR lpBuffer, LPDWORD lpnSize);
    BOOL WINAPI GetUserNameW(LPWSTR lpBuffer, LPDWORD lpnSize);

#ifdef UNICODE
#define GetUserName GetUserNameW
#else
#define GetUserName GetUserNameA
#endif // UNICODE

#define FILE_MAP_WRITE      SECTION_MAP_WRITE
#define FILE_MAP_READ       SECTION_MAP_READ
#define FILE_MAP_ALL_ACCESS SECTION_ALL_ACCESS

#define FILE_MAP_EXECUTE SECTION_MAP_EXECUTE_EXPLICIT // not included in FILE_MAP_ALL_ACCESS

#define FILE_MAP_COPY 0x00000001

#define FILE_MAP_RESERVE         0x80000000
#define FILE_MAP_TARGETS_INVALID 0x40000000
#define FILE_MAP_LARGE_PAGES     0x20000000

#define PAGE_NOACCESS          0x01
#define PAGE_READONLY          0x02
#define PAGE_READWRITE         0x04
#define PAGE_WRITECOPY         0x08
#define PAGE_EXECUTE           0x10
#define PAGE_EXECUTE_READ      0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_EXECUTE_WRITECOPY 0x80
#define PAGE_GUARD             0x100
#define PAGE_NOCACHE           0x200
#define PAGE_WRITECOMBINE      0x400

    HANDLE WINAPI OpenFileMappingA(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCSTR lpName);

    HANDLE WINAPI OpenFileMappingW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName);

    HANDLE WINAPI CreateFileMappingA(HANDLE hFile, LPSECURITY_ATTRIBUTES lpAttributes, DWORD flProtect, DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow, LPCSTR lpName);

    HANDLE WINAPI CreateFileMappingW(HANDLE hFile, LPSECURITY_ATTRIBUTES lpAttributes, DWORD flProtect, DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow, LPCWSTR lpName);

    LPVOID WINAPI MapViewOfFile(HANDLE hFileMappingObject, DWORD dwDesiredAccess, DWORD dwFileOffsetHigh, DWORD dwFileOffsetLow, size_t dwNumberOfBytesToMap);

    BOOL WINAPI UnmapViewOfFile(LPCVOID lpBaseAddress);

#ifdef UNICODE
#define OpenFileMapping   OpenFileMappingW
#define CreateFileMapping CreateFileMappingW
#else
#define OpenFileMapping   OpenFileMappingA
#define CreateFileMapping CreateFileMappingA
#endif // UNICODE

    BOOL WINAPI GetKeyboardState(PBYTE lpKeyState);

    SHORT WINAPI GetKeyState(int nVirtKey);
    SHORT WINAPI GetAsyncKeyState(int vKey);

    UINT WINAPI MapVirtualKey(UINT uCode, UINT uMapType);

    UINT WINAPI MapVirtualKeyEx(UINT uCode, UINT uMapType, HKL dwhkl);

    LONG WINAPI CompareFileTime(const FILETIME *ft1, const FILETIME *ft2);

    BOOL WINAPI SystemParametersInfoA(UINT action, UINT val, void *ptr, UINT winini);
    BOOL WINAPI SystemParametersInfoW(UINT action, UINT val, void *ptr, UINT winini);
#ifdef UNICODE
#define SystemParametersInfo SystemParametersInfoW
#else
#define SystemParametersInfo SystemParametersInfoA
#endif // UNICODE

    void WINAPI DebugBreak(void);

    BOOL WINAPI IsDebuggerPresent(void);

#define EXCEPTION_NONCONTINUABLE 0x1
#define EXCEPTION_MAXIMUM_PARAMETERS 15

    VOID WINAPI RaiseException(DWORD dwExceptionCode, DWORD dwExceptionFlags,
                               DWORD nNumberOfArguments, const ULONG_PTR *lpArguments);
    VOID WINAPI DbgBreakPoint(VOID);

    UINT WINAPI GetDoubleClickTime(VOID);
    BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency);
    BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount);

#ifndef NOCLIPBOARD

/*
 * Clipboard Manager Functions
 */
#define WINUSERAPI

    WINUSERAPI
    BOOL WINAPI OpenClipboard(_In_opt_ HWND hWndNewOwner);

    WINUSERAPI
    BOOL WINAPI CloseClipboard(VOID);

#if (WINVER >= 0x0500)

    WINUSERAPI
    DWORD
    WINAPI
    GetClipboardSequenceNumber(VOID);

#endif /* WINVER >= 0x0500 */

    WINUSERAPI
    HWND WINAPI GetClipboardOwner(VOID);

    WINUSERAPI
    HWND WINAPI SetClipboardViewer(_In_ HWND hWndNewViewer);

    WINUSERAPI
    HWND WINAPI GetClipboardViewer(VOID);

    WINUSERAPI
    BOOL WINAPI ChangeClipboardChain(_In_ HWND hWndRemove, _In_ HWND hWndNewNext);

    WINUSERAPI
    HANDLE
    WINAPI
    SetClipboardData(_In_ UINT uFormat, _In_opt_ HANDLE hMem);

    WINUSERAPI
    HANDLE
    WINAPI
    GetClipboardData(_In_ UINT uFormat);

    WINUSERAPI
    UINT WINAPI RegisterClipboardFormatA(_In_ LPCSTR lpszFormat);
    WINUSERAPI
    UINT WINAPI RegisterClipboardFormatW(_In_ LPCWSTR lpszFormat);
#ifdef UNICODE
#define RegisterClipboardFormat RegisterClipboardFormatW
#else
#define RegisterClipboardFormat RegisterClipboardFormatA
#endif // !UNICODE

    WINUSERAPI
    int WINAPI CountClipboardFormats(VOID);

    WINUSERAPI
    UINT WINAPI EnumClipboardFormats(_In_ UINT format);

    WINUSERAPI
    int WINAPI GetClipboardFormatNameA(_In_ UINT format, _Out_writes_(cchMaxCount) LPSTR lpszFormatName, _In_ int cchMaxCount);
    WINUSERAPI
    int WINAPI GetClipboardFormatNameW(_In_ UINT format, _Out_writes_(cchMaxCount) LPWSTR lpszFormatName, _In_ int cchMaxCount);
#ifdef UNICODE
#define GetClipboardFormatName GetClipboardFormatNameW
#else
#define GetClipboardFormatName GetClipboardFormatNameA
#endif // !UNICODE

    WINUSERAPI
    BOOL WINAPI EmptyClipboard(VOID);

    WINUSERAPI
    BOOL WINAPI IsClipboardFormatAvailable(_In_ UINT format);

    WINUSERAPI
    int WINAPI GetPriorityClipboardFormat(_In_reads_(cFormats) UINT *paFormatPriorityList, _In_ int cFormats);

    WINUSERAPI
    HWND WINAPI GetOpenClipboardWindow(VOID);

#endif /* !NOCLIPBOARD */

    DWORD
    WINAPI
    GetTempPathA(_In_ DWORD nBufferLength, _Out_writes_to_opt_(nBufferLength, return +1) LPSTR lpBuffer);

    DWORD
    WINAPI
    GetTempPathW(_In_ DWORD nBufferLength, _Out_writes_to_opt_(nBufferLength, return +1) LPWSTR lpBuffer);

    UINT WINAPI GetTempFileNameW(LPCWSTR lpPathName, LPCWSTR lpPrefixString, UINT uUnique, LPWSTR lpTempFileName);

    UINT WINAPI GetTempFileNameA(LPCSTR lpPathName, LPCSTR lpPrefixString, UINT uUnique, LPSTR lpTempFileName);
#ifdef UNICODE
#define GetTempPath     GetTempPathW
#define GetTempFileName GetTempFileNameW
#else
#define GetTempPath     GetTempPathA
#define GetTempFileName GetTempFileNameA
#endif

    BOOL WINAPI IsValidCodePage(UINT CodePage);

    UINT WINAPI GetKeyboardLayoutList(int nBuff, HKL *lpList);

    HKL WINAPI ActivateKeyboardLayout(HKL hkl, UINT Flags);

    UINT WINAPI GetACP(void);

    BOOL WINAPI IsDBCSLeadByte(BYTE c);

    HMODULE WINAPI GetModuleHandleA(LPCSTR lpModuleName);
    HMODULE WINAPI GetModuleHandleW(LPCWSTR lpModuleName);

#define GET_MODULE_HANDLE_EX_FLAG_PIN                (0x00000001)
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT (0x00000002)
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS       (0x00000004)

    BOOL WINAPI GetModuleHandleExA(_In_ DWORD dwFlags, _In_opt_ LPCSTR lpModuleName, _Out_ HMODULE *phModule);

    BOOL WINAPI GetModuleHandleExW(_In_ DWORD dwFlags, _In_opt_ LPCWSTR lpModuleName, _Out_ HMODULE *phModule);

#ifdef UNICODE
#define GetModuleHandleEx GetModuleHandleExW
#else
#define GetModuleHandleEx GetModuleHandleExA
#endif // !UNICODE

    BOOL WINAPI SetEnvironmentVariableA(LPCSTR lpName, LPCSTR lpValue);
    BOOL WINAPI SetEnvironmentVariableW(LPCWSTR lpName, LPCWSTR lpValue);

    DWORD WINAPI GetEnvironmentVariableA(LPCSTR lpName, LPSTR lpBuffer, DWORD nSize);
    DWORD WINAPI GetEnvironmentVariableW(LPCWSTR lpName, LPWSTR lpBuffer, DWORD nSize);

#ifdef UNICODE
#define SetEnvironmentVariable SetEnvironmentVariableW
#define GetEnvironmentVariable GetEnvironmentVariableW
#define GetModuleHandle        GetModuleHandleW
#else
#define SetEnvironmentVariable SetEnvironmentVariableA
#define GetEnvironmentVariable GetEnvironmentVariableA
#define GetModuleHandle        GetModuleHandleA
#endif // UNICODE

#if SFR_HOSTED
    /* host: MinGW <io.h> declares intptr_t _get_osfhandle(int); a shim
       declaration would clash with the CRT prototype */
#else
    HANDLE WINAPI _get_osfhandle(int fd);
#endif
    LPSTR WINAPI GetCommandLineA(void);
    LPWSTR WINAPI GetCommandLineW(void);

#ifdef UNICODE
#define GetCommandLine GetCommandLineW
#else
#define GetCommandLine GetCommandLineA
#endif // UNICODE

    HANDLE WINAPI FindFirstChangeNotificationA(LPCSTR lpPathName, BOOL bWatchSubtree, DWORD dwNotifyFilter);
    HANDLE WINAPI FindFirstChangeNotificationW(LPCWSTR lpPathName, BOOL bWatchSubtree, DWORD dwNotifyFilter);

    BOOL WINAPI FindNextChangeNotification(HANDLE hChangeHandle);

    BOOL WINAPI FindCloseChangeNotification(HANDLE hChangeHandle);

#ifdef UNICODE
#define FindFirstChangeNotification FindFirstChangeNotificationW
#else
#define FindFirstChangeNotification FindFirstChangeNotificationA
#endif // UNICODE

    typedef DWORD(WINAPI *PTHREAD_START_ROUTINE)(LPVOID lpThreadParameter);
    typedef PTHREAD_START_ROUTINE LPTHREAD_START_ROUTINE;

    HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes, SIZE_T dwStackSize, LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter, DWORD dwCreationFlags, tid_t *lpThreadId);

    // only support resume thread that was created with flag CREATE_SUSPENDED
    DWORD WINAPI ResumeThread(HANDLE hThread);

    DWORD WINAPI SuspendThread(HANDLE hThread);

    BOOL WINAPI TerminateThread(HANDLE hThread, DWORD dwExitCode);

    VOID WINAPI ExitThread(DWORD dwExitCode);

    // ------------------------------------------------------------------
    // CONTEXT / GetThreadContext
    //
    // Win32 exposes the register set of a suspended thread through CONTEXT.
    // WebKit's conservative stack scan (heap/MachineStackMarker.cpp) uses it
    // to find GC roots held in registers.  The layout below is the x86_64
    // (AMD64) integer/control subset of the Windows CONTEXT structure; the
    // field names and CONTEXT_* flag values match the SDK so callers compile
    // unchanged.  Floating point / debug registers are not exposed: no caller
    // in this tree scans them.
    // ------------------------------------------------------------------
#if defined(_M_X64) || defined(__x86_64__) || defined(__amd64__)

#define CONTEXT_AMD64 0x00100000L
#define CONTEXT_CONTROL (CONTEXT_AMD64 | 0x1L)
#define CONTEXT_INTEGER (CONTEXT_AMD64 | 0x2L)
#define CONTEXT_FLOATING_POINT (CONTEXT_AMD64 | 0x4L)
#define CONTEXT_DEBUG_REGISTERS (CONTEXT_AMD64 | 0x8L)
#define CONTEXT_FULL (CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT)
#define CONTEXT_ALL (CONTEXT_FULL | CONTEXT_DEBUG_REGISTERS)

    typedef struct _CONTEXT
    {
        DWORD ContextFlags;
        DWORD MxCsr;
        WORD SegCs;
        WORD SegDs;
        WORD SegEs;
        WORD SegFs;
        WORD SegGs;
        WORD SegSs;
        DWORD EFlags;
        DWORD64 Rax;
        DWORD64 Rcx;
        DWORD64 Rdx;
        DWORD64 Rbx;
        DWORD64 Rsp;
        DWORD64 Rbp;
        DWORD64 Rsi;
        DWORD64 Rdi;
        DWORD64 R8;
        DWORD64 R9;
        DWORD64 R10;
        DWORD64 R11;
        DWORD64 R12;
        DWORD64 R13;
        DWORD64 R14;
        DWORD64 R15;
        DWORD64 Rip;
    } CONTEXT, *PCONTEXT, *LPCONTEXT;

    // Fills lpContext with the register state of hThread (which must be
    // suspended by SuspendThread, exactly as the Win32 contract requires).
    BOOL WINAPI GetThreadContext(HANDLE hThread, LPCONTEXT lpContext);

#endif // x86_64
#ifdef __APPLE__
    BOOL WINAPI GetAppleBundlePath(char *path, int maxLen);
#endif //__APPLE__
#ifdef __cplusplus
}
#endif //__cplusplus

#endif //_SYSAPI_H_