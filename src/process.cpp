#include <windows.h>
#include <process.h>

struct ThreadParam
{
    void( __cdecl *start_address )( void * );
    void *arglist;
};
static DWORD WINAPI CDeclProc(LPVOID lpThreadParameter){
    ThreadParam *param = (ThreadParam *)lpThreadParameter;
    auto func = param->start_address;
    void *arglist = param->arglist;
    delete param;
    func(arglist);
    return 0;
}

uintptr_t _beginthread( // NATIVE CODE
   void( __cdecl *start_address )( void * ),
   unsigned stack_size,
   void *arglist
)
{
    ThreadParam *param = new ThreadParam();
    param->start_address = start_address;
    param->arglist = arglist;
    return (uintptr_t)CreateThread(NULL, stack_size, CDeclProc, param, 0, NULL);
}

struct ThreadParam2
{
    unsigned ( __stdcall *start_address )( void * );
    void *arglist;
};
static DWORD WINAPI StdCallProc(LPVOID lpThreadParameter){
    ThreadParam2 *param = (ThreadParam2 *)lpThreadParameter;
    auto func = param->start_address;
    void * arglist = param->arglist;
    delete param;
    return func(arglist);
}
uintptr_t _beginthreadex(
   void *security,
   unsigned int stack_size,
   unsigned int ( __stdcall *start_address )( void * ),
   void *arglist,
   unsigned int initflag,
   tid_t *thrdaddr
)
{
    ThreadParam2 *param = new ThreadParam2();
    param->start_address = start_address;
    param->arglist = arglist;
    return (uintptr_t)CreateThread(NULL, stack_size, StdCallProc, param, initflag, thrdaddr);
}

void _endthread( void )
{
    _endthreadex(0);
}

int _endthreadex(unsigned retval)
{
    ExitThread(retval);
    return 0;
}