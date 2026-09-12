#ifndef _SWINX_BSTR_INTERNAL_H_
#define _SWINX_BSTR_INTERNAL_H_

/* swinx-internal BSTR helpers, shared by objbase.cpp / variant.cpp /
   safearray.cpp. Not part of the Win32 API surface. */

#include <windows.h>

/* Deep copy a BSTR, preserving both the raw byte length and the buffer
   layout. SysAllocStringByteLen buffers carry raw bytes; regular BSTRs
   carry OLECHARs (platform wchar_t, 4 bytes on POSIX). A blind byte copy
   via SysAllocStringByteLen(src, SysStringByteLen(src)) only round-trips
   on platforms where sizeof(OLECHAR) == 2, so VariantCopy and the
   SafeArray BSTR copies must go through this helper instead. */
BSTR SysAllocStringCopy(BSTR src);

/* Number of raw bytes actually stored in the BSTR buffer: the byte length
   for SysAllocStringByteLen blobs, charCount * sizeof(OLECHAR) for regular
   BSTRs. Used by VectorFromBstr to keep the buffer <-> VT_UI1 array
   round-trip exact on every platform. */
UINT SysBstrRawByteCount(BSTR bstr);

#endif //_SWINX_BSTR_INTERNAL_H_
