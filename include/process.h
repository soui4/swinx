#ifndef _PROCESS_H_
#define _PROCESS_H_

uintptr_t _beginthread(void(__cdecl *start_address)(void *), unsigned int stack_size, void *arglist);

uintptr_t _beginthreadex(void *security, unsigned int stack_size, unsigned int(__stdcall *start_address)(void *), void *arglist, unsigned int initflag, tid_t *thrdaddr);

void _endthread(void);

int _endthreadex(unsigned retval);

#endif //_PROCESS_H_