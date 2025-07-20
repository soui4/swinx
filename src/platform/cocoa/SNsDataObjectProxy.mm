#include "SNsDataObjectProxy.h"
#include <shlobj.h>
#include <log.h>
#include <tostring.hpp>
#define kLogTag "SNsDataObjectProxy"

static NSString *const NSPasteboardTypeSOUIPrefix = @"com.soui.drag-and-drop_fmt";

SNsDataObjectProxy::SNsDataObjectProxy(NSPasteboard * nspasteboard)
    : m_nspasteboard(nspasteboard)
{
}

SNsDataObjectProxy::~SNsDataObjectProxy()
{
}

HRESULT SNsDataObjectProxy::GetData(FORMATETC *pformatetcIn, STGMEDIUM *pmedium)
{
    if (QueryGetData(pformatetcIn) != S_OK)
    {
        return DV_E_FORMATETC;
    }
    switch(pformatetcIn->cfFormat){
        case CF_TEXT:
        case CF_UNICODETEXT:
        {
            @autoreleasepool{
                NSString *str = nil;
                NSArray *filePaths = [m_nspasteboard propertyListForType:NSFilenamesPboardType];
                if(filePaths){
                    str = [filePaths componentsJoinedByString:@"\n"];
                }else{
                    str = [m_nspasteboard stringForType:NSStringPboardType];
                }
                if(!str)
                    return DV_E_DVASPECT;
                size_t len = [str lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
                const char *pstr = [str UTF8String];
                if(pformatetcIn->cfFormat == CF_TEXT){
                    pmedium->hGlobal = GlobalAlloc(0, len+1);
                    if (!pmedium->hGlobal)
                        return STG_E_MEDIUMFULL;
                    char *dst = (char*)GlobalLock(pmedium->hGlobal);
                    memcpy(dst, pstr, len);
                    dst[len]=0;//append a null
                    GlobalUnlock(pmedium->hGlobal);
                }else{
                    std::wstring wstr;
                    towstring(pstr,len,wstr);
                    pmedium->hGlobal = GlobalAlloc(0, (wstr.size()+1)*sizeof(wchar_t));
                    wchar_t* dst = (wchar_t*)GlobalLock(pmedium->hGlobal);
                    memcpy(dst, wstr.c_str(), wstr.size()*sizeof(wchar_t));
                    dst[wstr.size()]=0;
                    GlobalUnlock(pmedium->hGlobal);
                }
                return S_OK;
            }
            break;
        }
        default:
            {
                NSString *type = getPasteboardType(pformatetcIn->cfFormat);
                @autoreleasepool{
                    NSData *data = [m_nspasteboard dataForType:type];
                    if(!data)
                        return DV_E_DVASPECT;
                    pmedium->hGlobal = GlobalAlloc(0, [data length]);
                    if (!pmedium->hGlobal)
                        return STG_E_MEDIUMFULL;
                    void *dst = GlobalLock(pmedium->hGlobal);
                    memcpy(dst, [data bytes], [data length]);
                    GlobalUnlock(pmedium->hGlobal);
                }
            }
            break;
    }
    return DV_E_DVASPECT;
}

HRESULT SNsDataObjectProxy::EnumFormatEtc(
        /* [in] */ DWORD dwDirection,
        /* [out] */ __RPC__deref_out_opt IEnumFORMATETC** ppenumFormatEtc){
        
        if (dwDirection != DATADIR_GET) { 
            return E_NOTIMPL;
        }
        // 创建格式数组
        FORMATETC rgfmtetc[] = {
            { CF_TEXT, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL },
            { CF_UNICODETEXT, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL },
        };
        return SHCreateStdEnumFmtEtc(ARRAYSIZE(rgfmtetc), rgfmtetc, ppenumFormatEtc);
    }

    HRESULT WINAPI SNsDataObjectProxy::QueryGetData(
        /* [unique][in] */ __RPC__in_opt FORMATETC *pformatetc)
    {
        @autoreleasepool{
        if(!(pformatetc->tymed & TYMED_HGLOBAL))
            return DV_E_FORMATETC;
        switch(pformatetc->cfFormat){
            case CF_TEXT:
            case CF_UNICODETEXT:
            {
                NSArray *types = [m_nspasteboard types];
                if([types containsObject:NSStringPboardType])
                    return S_OK;
                if([types containsObject:NSFilenamesPboardType])
                    return S_OK;
                break;
            }
            default:
                {
                    NSArray *types = [m_nspasteboard types];
                    NSString *type = getPasteboardType(pformatetc->cfFormat);
                    if([types containsObject:type])
                        return S_OK;
                }
                break;
        }
        return DV_E_FORMATETC;
        }
    }

NSString *SNsDataObjectProxy::getPasteboardType(UINT uFormat)
{
    return [NSString stringWithFormat:@"%@/%u",NSPasteboardTypeSOUIPrefix,uFormat];
}