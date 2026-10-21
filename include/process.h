#ifndef _PROCESS_H_
#define _PROCESS_H_
#include <stdint.h>
// mingw's pthread.h pulls this header via <process.h>; keep the tid_t
// definition identical to ctypes.h/sdef.h (unsigned int under _WIN32).
#ifdef _WIN32
typedef unsigned int tid_t;
#else
typedef uintptr_t tid_t;
#endif
#ifdef __cplusplus
extern "C"
{
#endif //__cplusplus
    uintptr_t _beginthread(void(__cdecl *start_address)(void *), unsigned int stack_size, void *arglist);

    uintptr_t _beginthreadex(void *security, unsigned int stack_size, unsigned int(__stdcall *start_address)(void *), void *arglist, unsigned int initflag, tid_t *thrdaddr);

    void _endthread(void);

    int _endthreadex(unsigned retval);
#ifdef __cplusplus
}
#endif //__cplusplus
#endif //_PROCESS_H_