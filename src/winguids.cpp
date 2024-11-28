#include <windows.h>
#include <initguid.h>

DEFINE_GUID(GUID_NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
DEFINE_GUID(IID_IUnknown, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);
DEFINE_GUID(IID_IMalloc, 0x00000002, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
DEFINE_GUID(IID_IDropSource, 0x00000121, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
DEFINE_GUID(IID_IDropTarget, 0x00000122, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
DEFINE_GUID(IID_IDispatch, 0x00020400, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
DEFINE_GUID(IID_IMallocSpy, 0x0000001d, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
DEFINE_GUID(IID_ITypeInfo, 0x00020401, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
DEFINE_GUID(IID_ITypeLib, 0x00020402, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
DEFINE_GUID(IID_ITypeComp, 0x00020403, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
DEFINE_GUID(IID_IStream, 0x0000000c, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
DEFINE_GUID(IID_IStorage, 0x0000000b, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
DEFINE_GUID(IID_IEnumSTATSTG, 0x0000000d, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
DEFINE_GUID(IID_IEnumFORMATETC, 0x00000103, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
DEFINE_GUID(IID_IRecordInfo, 0x0000002F, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46);
DEFINE_GUID(IID_IAdviseSink, 0x0000010f, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46);
DEFINE_GUID(IID_IDataObject, 0x0000010e, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46);
DEFINE_GUID(IID_IEnumSTATDATA, 0x00000105, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46);

DEFINE_GUID(IID_IEnumGUID, 0x0002E000, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
DEFINE_GUID(IID_IEnumCATEGORYINFO, 0x0002E011, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
DEFINE_GUID(IID_ICatRegister, 0x0002E012, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
DEFINE_GUID(IID_ICatInformation, 0x0002E013, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);

