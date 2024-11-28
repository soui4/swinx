#ifndef _OLE2_H_
#define _OLE2_H_
#include <ctypes.h>
#include <objbase.h>

// View OBJECT Error Codes

#define E_DRAW                  VIEW_E_DRAW

// IDataObject Error Codes
#define DATA_E_FORMATETC        DV_E_FORMATETC

HRESULT CoInitialize(
    LPVOID pvReserved
);

HRESULT CoInitializeEx(
    void* pvReserved,
    DWORD dwCoInit
);

void CoUninitialize(void);

HRESULT RevokeDragDrop(
  HWND hwnd
);

HRESULT DoDragDrop(
  LPDATAOBJECT pDataObj,
  LPDROPSOURCE pDropSource,
  DWORD        dwOKEffects,
  LPDWORD      pdwEffect
);

typedef struct _OLESTREAM FAR* LPOLESTREAM;

typedef struct _OLESTREAMVTBL
{
    DWORD(CALLBACK* Get)(LPOLESTREAM, void FAR*, DWORD);
    DWORD(CALLBACK* Put)(LPOLESTREAM, const void FAR*, DWORD);
} OLESTREAMVTBL;
typedef  OLESTREAMVTBL FAR* LPOLESTREAMVTBL;

typedef struct _OLESTREAM
{
    LPOLESTREAMVTBL lpstbl;
} OLESTREAM;

/* verbs */
#define OLEIVERB_PRIMARY            (0L)
#define OLEIVERB_SHOW               (-1L)
#define OLEIVERB_OPEN               (-2L)
#define OLEIVERB_HIDE               (-3L)
#define OLEIVERB_UIACTIVATE         (-4L)
#define OLEIVERB_INPLACEACTIVATE    (-5L)
#define OLEIVERB_DISCARDUNDOSTATE   (-6L)


HRESULT WINAPI OleSetClipboard(IDataObject* pdo);

HRESULT WINAPI OleFlushClipboard(void);

HRESULT WINAPI OleIsCurrentClipboard(IDataObject* pdo);

HRESULT WINAPI OleGetClipboard(IDataObject** ppdo);

void WINAPI ReleaseStgMedium(STGMEDIUM* pmedium);

#endif//_OLE2_H_