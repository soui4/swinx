#import <Cocoa/Cocoa.h>
#import "helper.h"
#include <objidl.h>


BOOL EnumDataOjbect(IDataObject *pdo, FUNENUMDATOBOJECT fun, void *param){
    IEnumFORMATETC *enum_fmt;
    HRESULT hr = pdo->EnumFormatEtc(DATADIR_GET, &enum_fmt);
    if (FAILED(hr))
        return FALSE;
    FORMATETC fmt;
    while (enum_fmt->Next(1, &fmt, NULL) == S_OK)
    {
        STGMEDIUM medium;
        hr = pdo->GetData(&fmt, &medium);
        if (FAILED(hr))
            continue;
        if (medium.tymed != TYMED_HGLOBAL)
            continue;
        if (!fun(fmt.cfFormat, medium.hGlobal, param))
            break;
    }
    enum_fmt->Release();
    return TRUE;
}

