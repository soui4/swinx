#include "SUIDataObjectProxy.h"
#include <shlobj.h>
#include <log.h>
#include <tostring.hpp>
#include "atoms.h"
#define kLogTag "SUIDataObjectProxy"


SUIDataObjectProxy::SUIDataObjectProxy(UIPasteboard *pasteboard)
    : m_pasteboard(pasteboard)
{
}

SUIDataObjectProxy::~SUIDataObjectProxy()
{
}

HRESULT SUIDataObjectProxy::GetData(FORMATETC *pformatetcIn, STGMEDIUM *pmedium)
{
    return E_NOTIMPL;
    /*
    if (QueryGetData(pformatetcIn) != S_OK)
    {
        return DV_E_FORMATETC;
    }
    switch(pformatetcIn->cfFormat){
        case CF_TEXT:
        case CF_UNICODETEXT:
        {
            @autoreleasepool{
                NSString *str = [m_pasteboard stringForType:UIPasteboardTypeAutomatic];
                if(!str) {
                    // 尝试从 URL 列表拼接
                    NSArray<NSURL*> *urls = [m_pasteboard URLs];
                    if(urls.count > 0){
                        NSMutableArray *paths = [NSMutableArray array];
                        for(NSURL *url in urls){
                            if(url.isFileURL)
                                [paths addObject:url.path];
                        }
                        str = [paths componentsJoinedByString:@"\n"];
                    }
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
                    dst[len]=0;
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
                pmedium->tymed = TYMED_HGLOBAL;
                return S_OK;
            }
            break;
        }
        default:
            {
                NSString *type = getPasteboardType(pformatetcIn->cfFormat);
                @autoreleasepool{
                    NSData *data = [m_pasteboard dataForType:type];
                    if(!data)
                        return DV_E_DVASPECT;
                    pmedium->hGlobal = GlobalAlloc(0, [data length]);
                    if (!pmedium->hGlobal)
                        return STG_E_MEDIUMFULL;
                    void *dst = GlobalLock(pmedium->hGlobal);
                    memcpy(dst, [data bytes], [data length]);
                    GlobalUnlock(pmedium->hGlobal);
                    pmedium->tymed = TYMED_HGLOBAL;
                    return S_OK;
                }
            }
            break;
    }
    return DV_E_DVASPECT;
     */
}

HRESULT SUIDataObjectProxy::EnumFormatEtc(
        DWORD dwDirection,
        __RPC__deref_out_opt IEnumFORMATETC** ppenumFormatEtc){

        if (dwDirection != DATADIR_GET) {
            return E_NOTIMPL;
        }
        FORMATETC rgfmtetc[] = {
            { CF_TEXT, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL },
            { CF_UNICODETEXT, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL },
        };
        return SHCreateStdEnumFmtEtc(ARRAYSIZE(rgfmtetc), rgfmtetc, ppenumFormatEtc);
    }

HRESULT WINAPI SUIDataObjectProxy::QueryGetData(
        __RPC__in_opt FORMATETC *pformatetc)
    {
    return E_NOTIMPL;
    /*
        @autoreleasepool{
        if(!(pformatetc->tymed & TYMED_HGLOBAL))
            return DV_E_FORMATETC;
        switch(pformatetc->cfFormat){
            case CF_TEXT:
            case CF_UNICODETEXT:
            {
                if([m_pasteboard stringForType:UIPasteboardTypeAutomatic] != nil)
                    return S_OK;
                if([m_pasteboard URLs].count > 0)
                    return S_OK;
                break;
            }
            default:
                {
                    NSArray *types = [m_pasteboard pasteboardTypes];
                    NSString *type = getPasteboardType(pformatetc->cfFormat);
                    if([types containsObject:type])
                        return S_OK;
                }
                break;
        }
        return DV_E_FORMATETC;
        }
     */
    }

NSString *SUIDataObjectProxy::getPasteboardType(UINT uFormat)
{
  switch (uFormat) {
  case CF_TEXT:
    return UIPasteboardTypeAutomatic;
  case CF_BITMAP:
//    return UIPasteboardTypePNG;
  default:
    return [NSString stringWithFormat:@"com.swinx.clipboard.format.%d", uFormat];
  }
}
