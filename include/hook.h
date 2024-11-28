#ifndef _HOOK_H_
#define _HOOK_H_

typedef LRESULT(CALLBACK *HOOKPROC)(int code, WPARAM wParam, LPARAM lParam);
typedef struct hook *HHOOK;

HHOOK WINAPI SetWindowsHookA(INT id, HOOKPROC proc);

HHOOK WINAPI SetWindowsHookW(INT id, HOOKPROC proc);

HHOOK
WINAPI SetWindowsHookExA(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId);

HHOOK
WINAPI SetWindowsHookExW(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId);

BOOL WINAPI UnhookWindowsHookEx(HHOOK hhk);

LRESULT
WINAPI CallNextHookEx(HHOOK hhk, int nCode, WPARAM wParam, LPARAM lParam);

#endif //_HOOK_H_