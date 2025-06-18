#ifndef __HELPER_H__
#define __HELPER_H__

#include <ctypes.h>
#include <sysapi.h>
#include <winerror.h>

struct IDataObject;

typedef BOOL (*FUNENUMDATOBOJECT)(WORD fmt, HGLOBAL hMem, void *param);
BOOL EnumDataOjbect(IDataObject *pdo, FUNENUMDATOBOJECT fun, void *param);


#endif//__HELPER_H__