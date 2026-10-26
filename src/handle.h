#ifndef _HANDLE_H_
#define _HANDLE_H_

#include <mutex>

typedef void (*FreeHandlePtr)(void *ptr);
typedef struct _Handle
{
    std::recursive_mutex mutex;
    int type;
    int nRef;
    void *ptr;
    FreeHandlePtr cbFree;

    _Handle(int _type, void *_ptr, FreeHandlePtr _cbFree);
    virtual ~_Handle();
} * HANDLE;

enum
{
    GDI_OBJ = 1,

    FILE_OBJ = 20,
    HEAP_OBJ = 21,
    SYN_OBJ = 100,
    FILEMAP_OBJ = 200,
    /* RegisterWaitForSingleObject 交出的等待句柄；不参与 GetSynHandle 的
     * 同步对象体系，只能被 UnregisterWait / CloseHandle 释放。 */
    WAIT_OBJ = 300,
};

/* GDI 句柄对外的类型与 Windows SDK 一致：HGDIOBJ 是 void*，HBITMAP/HPEN/...
 * 是各自独立的结构体指针，所以任何具体句柄都能隐式转成 HGDIOBJ，但反过来
 * （void* -> HFONT 等）需要显式转换。swinx 内部这些句柄指向的对象都是同一个
 * _Handle 包装，凡是需要读写包装成员（type/ptr/nRef）的地方统一走这里。 */
inline HANDLE ToGdiHandle(void *h)
{
    return (HANDLE)h;
}

HANDLE WINAPI AddHandleRef(HANDLE hHandle);

/* GDI 对象计数，GetGuiResources(GR_GDIOBJECTS) 的数据来源：_Handle 包装的
 * GDI 对象（画笔/画刷/字体/位图/区域）和 HDC 都算，与 Windows 的口径一致。 */
void AddSwinxGdiObject();
void ReleaseSwinxGdiObject();
int GetSwinxGdiObjectCount();

BOOL WINAPI CloseHandle(HANDLE h);

HANDLE InitHandle(int type, void *ptr, FreeHandlePtr cbFree);

#endif //_HANDLE_H_