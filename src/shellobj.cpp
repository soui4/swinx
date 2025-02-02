#include <shlobj.h>
#include "enumformatetc.h"

HRESULT SHCreateStdEnumFmtEtc(UINT cfmt, const FORMATETC afmt[], IEnumFORMATETC **ppenumFormatEtc)
{
    *ppenumFormatEtc = new SEnumFormatEtc(cfmt, afmt);
    if (!(*ppenumFormatEtc))
        return E_OUTOFMEMORY;
    return S_OK;
}
