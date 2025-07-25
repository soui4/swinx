#include <windows.h>

/* this ALWAYS GENERATED file contains the definitions for the interfaces */

/* File created by MIDL compiler version 8.01.0622 */
/* @@MIDL_FILE_HEADING(  ) */

/* verify that the <rpcndr.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 500
#endif

/* verify that the <rpcsal.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCSAL_H_VERSION__
#define __REQUIRED_RPCSAL_H_VERSION__ 100
#endif

#ifndef __oleidl_h__
#define __oleidl_h__

/* Forward Declarations */

#ifndef __IOleAdviseHolder_FWD_DEFINED__
#define __IOleAdviseHolder_FWD_DEFINED__
typedef interface IOleAdviseHolder IOleAdviseHolder;

#endif /* __IOleAdviseHolder_FWD_DEFINED__ */

#ifndef __IOleCache_FWD_DEFINED__
#define __IOleCache_FWD_DEFINED__
typedef interface IOleCache IOleCache;

#endif /* __IOleCache_FWD_DEFINED__ */

#ifndef __IOleCache2_FWD_DEFINED__
#define __IOleCache2_FWD_DEFINED__
typedef interface IOleCache2 IOleCache2;

#endif /* __IOleCache2_FWD_DEFINED__ */

#ifndef __IOleCacheControl_FWD_DEFINED__
#define __IOleCacheControl_FWD_DEFINED__
typedef interface IOleCacheControl IOleCacheControl;

#endif /* __IOleCacheControl_FWD_DEFINED__ */

#ifndef __IParseDisplayName_FWD_DEFINED__
#define __IParseDisplayName_FWD_DEFINED__
typedef interface IParseDisplayName IParseDisplayName;

#endif /* __IParseDisplayName_FWD_DEFINED__ */

#ifndef __IOleContainer_FWD_DEFINED__
#define __IOleContainer_FWD_DEFINED__
typedef interface IOleContainer IOleContainer;

#endif /* __IOleContainer_FWD_DEFINED__ */

#ifndef __IOleClientSite_FWD_DEFINED__
#define __IOleClientSite_FWD_DEFINED__
typedef interface IOleClientSite IOleClientSite;

#endif /* __IOleClientSite_FWD_DEFINED__ */

#ifndef __IOleObject_FWD_DEFINED__
#define __IOleObject_FWD_DEFINED__
typedef interface IOleObject IOleObject;

#endif /* __IOleObject_FWD_DEFINED__ */

#ifndef __IOleWindow_FWD_DEFINED__
#define __IOleWindow_FWD_DEFINED__
typedef interface IOleWindow IOleWindow;

#endif /* __IOleWindow_FWD_DEFINED__ */

#ifndef __IOleLink_FWD_DEFINED__
#define __IOleLink_FWD_DEFINED__
typedef interface IOleLink IOleLink;

#endif /* __IOleLink_FWD_DEFINED__ */

#ifndef __IOleItemContainer_FWD_DEFINED__
#define __IOleItemContainer_FWD_DEFINED__
typedef interface IOleItemContainer IOleItemContainer;

#endif /* __IOleItemContainer_FWD_DEFINED__ */

#ifndef __IOleInPlaceUIWindow_FWD_DEFINED__
#define __IOleInPlaceUIWindow_FWD_DEFINED__
typedef interface IOleInPlaceUIWindow IOleInPlaceUIWindow;

#endif /* __IOleInPlaceUIWindow_FWD_DEFINED__ */

#ifndef __IOleInPlaceActiveObject_FWD_DEFINED__
#define __IOleInPlaceActiveObject_FWD_DEFINED__
typedef interface IOleInPlaceActiveObject IOleInPlaceActiveObject;

#endif /* __IOleInPlaceActiveObject_FWD_DEFINED__ */

#ifndef __IOleInPlaceFrame_FWD_DEFINED__
#define __IOleInPlaceFrame_FWD_DEFINED__
typedef interface IOleInPlaceFrame IOleInPlaceFrame;

#endif /* __IOleInPlaceFrame_FWD_DEFINED__ */

#ifndef __IOleInPlaceObject_FWD_DEFINED__
#define __IOleInPlaceObject_FWD_DEFINED__
typedef interface IOleInPlaceObject IOleInPlaceObject;

#endif /* __IOleInPlaceObject_FWD_DEFINED__ */

#ifndef __IOleInPlaceSite_FWD_DEFINED__
#define __IOleInPlaceSite_FWD_DEFINED__
typedef interface IOleInPlaceSite IOleInPlaceSite;

#endif /* __IOleInPlaceSite_FWD_DEFINED__ */

#ifndef __IContinue_FWD_DEFINED__
#define __IContinue_FWD_DEFINED__
typedef interface IContinue IContinue;

#endif /* __IContinue_FWD_DEFINED__ */

#ifndef __IViewObject_FWD_DEFINED__
#define __IViewObject_FWD_DEFINED__
typedef interface IViewObject IViewObject;

#endif /* __IViewObject_FWD_DEFINED__ */

#ifndef __IViewObject2_FWD_DEFINED__
#define __IViewObject2_FWD_DEFINED__
typedef interface IViewObject2 IViewObject2;

#endif /* __IViewObject2_FWD_DEFINED__ */

#ifndef __IDropSource_FWD_DEFINED__
#define __IDropSource_FWD_DEFINED__
typedef interface IDropSource IDropSource;

#endif /* __IDropSource_FWD_DEFINED__ */

#ifndef __IDropTarget_FWD_DEFINED__
#define __IDropTarget_FWD_DEFINED__
typedef interface IDropTarget IDropTarget;

#endif /* __IDropTarget_FWD_DEFINED__ */

#ifndef __IDropSourceNotify_FWD_DEFINED__
#define __IDropSourceNotify_FWD_DEFINED__
typedef interface IDropSourceNotify IDropSourceNotify;

#endif /* __IDropSourceNotify_FWD_DEFINED__ */

#ifndef __IEnterpriseDropTarget_FWD_DEFINED__
#define __IEnterpriseDropTarget_FWD_DEFINED__
typedef interface IEnterpriseDropTarget IEnterpriseDropTarget;

#endif /* __IEnterpriseDropTarget_FWD_DEFINED__ */

#ifndef __IEnumOLEVERB_FWD_DEFINED__
#define __IEnumOLEVERB_FWD_DEFINED__
typedef interface IEnumOLEVERB IEnumOLEVERB;

#endif /* __IEnumOLEVERB_FWD_DEFINED__ */

/* header files for imported files */
#include "objidl.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* interface __MIDL_itf_oleidl_0000_0000 */
/* [local] */

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (c) Microsoft Corporation. All rights reserved.
//
//--------------------------------------------------------------------------
#ifndef __IOleAdviseHolder_INTERFACE_DEFINED__
#define __IOleAdviseHolder_INTERFACE_DEFINED__

    /* interface IOleAdviseHolder */
    /* [uuid][object][local] */

    typedef /* [unique] */ IOleAdviseHolder *LPOLEADVISEHOLDER;

#if defined(__cplusplus) && !defined(CINTERFACE)

    DEFINE_GUID(IID_IOleAdviseHolder, 0x00000111, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
    struct IOleAdviseHolder : public IUnknown
    {
      public:
        virtual HRESULT STDMETHODCALLTYPE Advise(
            /* [annotation][unique][in] */
            _In_ IAdviseSink *pAdvise,
            /* [annotation][out] */
            _Out_ DWORD *pdwConnection)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE Unadvise(
            /* [in] */ DWORD dwConnection)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE EnumAdvise(
            /* [annotation][out] */
            _Outptr_ IEnumSTATDATA **ppenumAdvise)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE SendOnRename(
            /* [annotation][unique][in] */
            _In_ IMoniker *pmk)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE SendOnSave(void) = 0;

        virtual HRESULT STDMETHODCALLTYPE SendOnClose(void) = 0;
    };

#else /* C style interface */

    typedef struct IOleAdviseHolderVtbl
    {
        BEGIN_INTERFACE

        HRESULT(STDMETHODCALLTYPE *QueryInterface)
        (IOleAdviseHolder *This,
         /* [in] */ REFIID riid,
         /* [annotation][iid_is][out] */
         _COM_Outptr_ void **ppvObject);

        ULONG(STDMETHODCALLTYPE *AddRef)(IOleAdviseHolder *This);

        ULONG(STDMETHODCALLTYPE *Release)(IOleAdviseHolder *This);

        HRESULT(STDMETHODCALLTYPE *Advise)
        (IOleAdviseHolder *This,
         /* [annotation][unique][in] */
         _In_ IAdviseSink *pAdvise,
         /* [annotation][out] */
         _Out_ DWORD *pdwConnection);

        HRESULT(STDMETHODCALLTYPE *Unadvise)
        (IOleAdviseHolder *This,
         /* [in] */ DWORD dwConnection);

        HRESULT(STDMETHODCALLTYPE *EnumAdvise)
        (IOleAdviseHolder *This,
         /* [annotation][out] */
         _Outptr_ IEnumSTATDATA **ppenumAdvise);

        HRESULT(STDMETHODCALLTYPE *SendOnRename)
        (IOleAdviseHolder *This,
         /* [annotation][unique][in] */
         _In_ IMoniker *pmk);

        HRESULT(STDMETHODCALLTYPE *SendOnSave)(IOleAdviseHolder *This);

        HRESULT(STDMETHODCALLTYPE *SendOnClose)(IOleAdviseHolder *This);

        END_INTERFACE
    } IOleAdviseHolderVtbl;

    interface IOleAdviseHolder
    {
        CONST_VTBL struct IOleAdviseHolderVtbl *lpVtbl;
    };

#ifdef COBJMACROS

#define IOleAdviseHolder_QueryInterface(This, riid, ppvObject) ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IOleAdviseHolder_AddRef(This) ((This)->lpVtbl->AddRef(This))

#define IOleAdviseHolder_Release(This) ((This)->lpVtbl->Release(This))

#define IOleAdviseHolder_Advise(This, pAdvise, pdwConnection) ((This)->lpVtbl->Advise(This, pAdvise, pdwConnection))

#define IOleAdviseHolder_Unadvise(This, dwConnection) ((This)->lpVtbl->Unadvise(This, dwConnection))

#define IOleAdviseHolder_EnumAdvise(This, ppenumAdvise) ((This)->lpVtbl->EnumAdvise(This, ppenumAdvise))

#define IOleAdviseHolder_SendOnRename(This, pmk) ((This)->lpVtbl->SendOnRename(This, pmk))

#define IOleAdviseHolder_SendOnSave(This) ((This)->lpVtbl->SendOnSave(This))

#define IOleAdviseHolder_SendOnClose(This) ((This)->lpVtbl->SendOnClose(This))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IOleAdviseHolder_INTERFACE_DEFINED__ */

    /* interface __MIDL_itf_oleidl_0000_0001 */
    /* [local] */

#ifndef __IOleCache_INTERFACE_DEFINED__
#define __IOleCache_INTERFACE_DEFINED__

    /* interface IOleCache */
    /* [unique][uuid][object] */

    typedef /* [unique] */ __RPC_unique_pointer IOleCache *LPOLECACHE;

#if defined(__cplusplus) && !defined(CINTERFACE)

    DEFINE_GUID(IID_IOleCache, 0x0000011e, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
    struct IOleCache : public IUnknown
    {
      public:
        virtual HRESULT STDMETHODCALLTYPE Cache(
            /* [unique][in] */ __RPC__in_opt FORMATETC *pformatetc,
            /* [in] */ DWORD advf,
            /* [out] */ __RPC__out DWORD *pdwConnection)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE Uncache(
            /* [in] */ DWORD dwConnection)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE EnumCache(
            /* [out] */ __RPC__deref_out_opt IEnumSTATDATA **ppenumSTATDATA)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE InitCache(
            /* [unique][in] */ __RPC__in_opt IDataObject *pDataObject)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE SetData(
            /* [unique][in] */ __RPC__in_opt FORMATETC *pformatetc,
            /* [unique][in] */ __RPC__in_opt STGMEDIUM *pmedium,
            /* [in] */ BOOL fRelease)
            = 0;
    };

#else /* C style interface */

    typedef struct IOleCacheVtbl
    {
        BEGIN_INTERFACE

        HRESULT(STDMETHODCALLTYPE *QueryInterface)
        (__RPC__in IOleCache *This,
         /* [in] */ __RPC__in REFIID riid,
         /* [annotation][iid_is][out] */
         _COM_Outptr_ void **ppvObject);

        ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IOleCache *This);

        ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IOleCache *This);

        HRESULT(STDMETHODCALLTYPE *Cache)
        (__RPC__in IOleCache *This,
         /* [unique][in] */ __RPC__in_opt FORMATETC *pformatetc,
         /* [in] */ DWORD advf,
         /* [out] */ __RPC__out DWORD *pdwConnection);

        HRESULT(STDMETHODCALLTYPE *Uncache)
        (__RPC__in IOleCache *This,
         /* [in] */ DWORD dwConnection);

        HRESULT(STDMETHODCALLTYPE *EnumCache)
        (__RPC__in IOleCache *This,
         /* [out] */ __RPC__deref_out_opt IEnumSTATDATA **ppenumSTATDATA);

        HRESULT(STDMETHODCALLTYPE *InitCache)
        (__RPC__in IOleCache *This,
         /* [unique][in] */ __RPC__in_opt IDataObject *pDataObject);

        HRESULT(STDMETHODCALLTYPE *SetData)
        (__RPC__in IOleCache *This,
         /* [unique][in] */ __RPC__in_opt FORMATETC *pformatetc,
         /* [unique][in] */ __RPC__in_opt STGMEDIUM *pmedium,
         /* [in] */ BOOL fRelease);

        END_INTERFACE
    } IOleCacheVtbl;

    interface IOleCache
    {
        CONST_VTBL struct IOleCacheVtbl *lpVtbl;
    };

#ifdef COBJMACROS

#define IOleCache_QueryInterface(This, riid, ppvObject) ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IOleCache_AddRef(This) ((This)->lpVtbl->AddRef(This))

#define IOleCache_Release(This) ((This)->lpVtbl->Release(This))

#define IOleCache_Cache(This, pformatetc, advf, pdwConnection) ((This)->lpVtbl->Cache(This, pformatetc, advf, pdwConnection))

#define IOleCache_Uncache(This, dwConnection) ((This)->lpVtbl->Uncache(This, dwConnection))

#define IOleCache_EnumCache(This, ppenumSTATDATA) ((This)->lpVtbl->EnumCache(This, ppenumSTATDATA))

#define IOleCache_InitCache(This, pDataObject) ((This)->lpVtbl->InitCache(This, pDataObject))

#define IOleCache_SetData(This, pformatetc, pmedium, fRelease) ((This)->lpVtbl->SetData(This, pformatetc, pmedium, fRelease))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IOleCache_INTERFACE_DEFINED__ */

#ifndef __IOleCache2_INTERFACE_DEFINED__
#define __IOleCache2_INTERFACE_DEFINED__

    /* interface IOleCache2 */
    /* [unique][uuid][object] */

    typedef /* [unique] */ __RPC_unique_pointer IOleCache2 *LPOLECACHE2;

#define UPDFCACHE_NODATACACHE (0x1)

#define UPDFCACHE_ONSAVECACHE (0x2)

#define UPDFCACHE_ONSTOPCACHE (0x4)

#define UPDFCACHE_NORMALCACHE (0x8)

#define UPDFCACHE_IFBLANK (0x10)

#define UPDFCACHE_ONLYIFBLANK (0x80000000)

#define UPDFCACHE_IFBLANKORONSAVECACHE ((UPDFCACHE_IFBLANK | UPDFCACHE_ONSAVECACHE))

#define UPDFCACHE_ALL ((DWORD)~UPDFCACHE_ONLYIFBLANK)

#define UPDFCACHE_ALLBUTNODATACACHE ((UPDFCACHE_ALL & (DWORD)~UPDFCACHE_NODATACACHE))

    typedef /* [v1_enum] */
        enum tagDISCARDCACHE
    {
        DISCARDCACHE_SAVEIFDIRTY = 0,
        DISCARDCACHE_NOSAVE = 1
    } DISCARDCACHE;

#if defined(__cplusplus) && !defined(CINTERFACE)

    DEFINE_GUID(IID_IOleCache2, 0x00000128, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
    struct IOleCache2 : public IOleCache
    {
      public:
        virtual /* [local] */ HRESULT STDMETHODCALLTYPE UpdateCache(
            /* [annotation][in] */
            _In_ LPDATAOBJECT pDataObject,
            /* [annotation][in] */
            _In_ DWORD grfUpdf,
            /* [annotation][in] */
            _Reserved_ LPVOID pReserved)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE DiscardCache(
            /* [in] */ DWORD dwDiscardOptions)
            = 0;
    };

#else /* C style interface */

    typedef struct IOleCache2Vtbl
    {
        BEGIN_INTERFACE

        HRESULT(STDMETHODCALLTYPE *QueryInterface)
        (__RPC__in IOleCache2 *This,
         /* [in] */ __RPC__in REFIID riid,
         /* [annotation][iid_is][out] */
         _COM_Outptr_ void **ppvObject);

        ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IOleCache2 *This);

        ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IOleCache2 *This);

        HRESULT(STDMETHODCALLTYPE *Cache)
        (__RPC__in IOleCache2 *This,
         /* [unique][in] */ __RPC__in_opt FORMATETC *pformatetc,
         /* [in] */ DWORD advf,
         /* [out] */ __RPC__out DWORD *pdwConnection);

        HRESULT(STDMETHODCALLTYPE *Uncache)
        (__RPC__in IOleCache2 *This,
         /* [in] */ DWORD dwConnection);

        HRESULT(STDMETHODCALLTYPE *EnumCache)
        (__RPC__in IOleCache2 *This,
         /* [out] */ __RPC__deref_out_opt IEnumSTATDATA **ppenumSTATDATA);

        HRESULT(STDMETHODCALLTYPE *InitCache)
        (__RPC__in IOleCache2 *This,
         /* [unique][in] */ __RPC__in_opt IDataObject *pDataObject);

        HRESULT(STDMETHODCALLTYPE *SetData)
        (__RPC__in IOleCache2 *This,
         /* [unique][in] */ __RPC__in_opt FORMATETC *pformatetc,
         /* [unique][in] */ __RPC__in_opt STGMEDIUM *pmedium,
         /* [in] */ BOOL fRelease);

        /* [local] */ HRESULT(STDMETHODCALLTYPE *UpdateCache)(IOleCache2 *This,
                                                              /* [annotation][in] */
                                                              _In_ LPDATAOBJECT pDataObject,
                                                              /* [annotation][in] */
                                                              _In_ DWORD grfUpdf,
                                                              /* [annotation][in] */
                                                              _Reserved_ LPVOID pReserved);

        HRESULT(STDMETHODCALLTYPE *DiscardCache)
        (__RPC__in IOleCache2 *This,
         /* [in] */ DWORD dwDiscardOptions);

        END_INTERFACE
    } IOleCache2Vtbl;

    interface IOleCache2
    {
        CONST_VTBL struct IOleCache2Vtbl *lpVtbl;
    };

#ifdef COBJMACROS

#define IOleCache2_QueryInterface(This, riid, ppvObject) ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IOleCache2_AddRef(This) ((This)->lpVtbl->AddRef(This))

#define IOleCache2_Release(This) ((This)->lpVtbl->Release(This))

#define IOleCache2_Cache(This, pformatetc, advf, pdwConnection) ((This)->lpVtbl->Cache(This, pformatetc, advf, pdwConnection))

#define IOleCache2_Uncache(This, dwConnection) ((This)->lpVtbl->Uncache(This, dwConnection))

#define IOleCache2_EnumCache(This, ppenumSTATDATA) ((This)->lpVtbl->EnumCache(This, ppenumSTATDATA))

#define IOleCache2_InitCache(This, pDataObject) ((This)->lpVtbl->InitCache(This, pDataObject))

#define IOleCache2_SetData(This, pformatetc, pmedium, fRelease) ((This)->lpVtbl->SetData(This, pformatetc, pmedium, fRelease))

#define IOleCache2_UpdateCache(This, pDataObject, grfUpdf, pReserved) ((This)->lpVtbl->UpdateCache(This, pDataObject, grfUpdf, pReserved))

#define IOleCache2_DiscardCache(This, dwDiscardOptions) ((This)->lpVtbl->DiscardCache(This, dwDiscardOptions))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IOleCache2_INTERFACE_DEFINED__ */

#ifndef __IOleCacheControl_INTERFACE_DEFINED__
#define __IOleCacheControl_INTERFACE_DEFINED__

    /* interface IOleCacheControl */
    /* [uuid][object] */

    typedef /* [unique] */ __RPC_unique_pointer IOleCacheControl *LPOLECACHECONTROL;

#if defined(__cplusplus) && !defined(CINTERFACE)

    DEFINE_GUID(IID_IOleCacheControl, 0x00000129, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
    struct IOleCacheControl : public IUnknown
    {
      public:
        virtual HRESULT STDMETHODCALLTYPE OnRun(__RPC__in_opt LPDATAOBJECT pDataObject) = 0;

        virtual HRESULT STDMETHODCALLTYPE OnStop(void) = 0;
    };

#else /* C style interface */

    typedef struct IOleCacheControlVtbl
    {
        BEGIN_INTERFACE

        HRESULT(STDMETHODCALLTYPE *QueryInterface)
        (__RPC__in IOleCacheControl *This,
         /* [in] */ __RPC__in REFIID riid,
         /* [annotation][iid_is][out] */
         _COM_Outptr_ void **ppvObject);

        ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IOleCacheControl *This);

        ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IOleCacheControl *This);

        HRESULT(STDMETHODCALLTYPE *OnRun)(__RPC__in IOleCacheControl *This, __RPC__in_opt LPDATAOBJECT pDataObject);

        HRESULT(STDMETHODCALLTYPE *OnStop)(__RPC__in IOleCacheControl *This);

        END_INTERFACE
    } IOleCacheControlVtbl;

    interface IOleCacheControl
    {
        CONST_VTBL struct IOleCacheControlVtbl *lpVtbl;
    };

#ifdef COBJMACROS

#define IOleCacheControl_QueryInterface(This, riid, ppvObject) ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IOleCacheControl_AddRef(This) ((This)->lpVtbl->AddRef(This))

#define IOleCacheControl_Release(This) ((This)->lpVtbl->Release(This))

#define IOleCacheControl_OnRun(This, pDataObject) ((This)->lpVtbl->OnRun(This, pDataObject))

#define IOleCacheControl_OnStop(This) ((This)->lpVtbl->OnStop(This))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IOleCacheControl_INTERFACE_DEFINED__ */

#ifndef __IParseDisplayName_INTERFACE_DEFINED__
#define __IParseDisplayName_INTERFACE_DEFINED__

    /* interface IParseDisplayName */
    /* [unique][uuid][object] */

    typedef /* [unique] */ __RPC_unique_pointer IParseDisplayName *LPPARSEDISPLAYNAME;

#if defined(__cplusplus) && !defined(CINTERFACE)

    DEFINE_GUID(IID_IParseDisplayName, 0x0000011a, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
    struct IParseDisplayName : public IUnknown
    {
      public:
        virtual HRESULT STDMETHODCALLTYPE ParseDisplayName(
            /* [unique][in] */ __RPC__in_opt IBindCtx *pbc,
            /* [in] */ __RPC__in LPOLESTR pszDisplayName,
            /* [out] */ __RPC__out ULONG *pchEaten,
            /* [out] */ __RPC__deref_out_opt IMoniker **ppmkOut)
            = 0;
    };

#else /* C style interface */

    typedef struct IParseDisplayNameVtbl
    {
        BEGIN_INTERFACE

        HRESULT(STDMETHODCALLTYPE *QueryInterface)
        (__RPC__in IParseDisplayName *This,
         /* [in] */ __RPC__in REFIID riid,
         /* [annotation][iid_is][out] */
         _COM_Outptr_ void **ppvObject);

        ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IParseDisplayName *This);

        ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IParseDisplayName *This);

        HRESULT(STDMETHODCALLTYPE *ParseDisplayName)
        (__RPC__in IParseDisplayName *This,
         /* [unique][in] */ __RPC__in_opt IBindCtx *pbc,
         /* [in] */ __RPC__in LPOLESTR pszDisplayName,
         /* [out] */ __RPC__out ULONG *pchEaten,
         /* [out] */ __RPC__deref_out_opt IMoniker **ppmkOut);

        END_INTERFACE
    } IParseDisplayNameVtbl;

    interface IParseDisplayName
    {
        CONST_VTBL struct IParseDisplayNameVtbl *lpVtbl;
    };

#ifdef COBJMACROS

#define IParseDisplayName_QueryInterface(This, riid, ppvObject) ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IParseDisplayName_AddRef(This) ((This)->lpVtbl->AddRef(This))

#define IParseDisplayName_Release(This) ((This)->lpVtbl->Release(This))

#define IParseDisplayName_ParseDisplayName(This, pbc, pszDisplayName, pchEaten, ppmkOut) ((This)->lpVtbl->ParseDisplayName(This, pbc, pszDisplayName, pchEaten, ppmkOut))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IParseDisplayName_INTERFACE_DEFINED__ */

#ifndef __IEnumUnknown_INTERFACE_DEFINED__
#define __IEnumUnknown_INTERFACE_DEFINED__

    /* interface IEnumUnknown */
    /* [unique][uuid][object] */

    typedef /* [unique] */ __RPC_unique_pointer struct IEnumUnknown *LPENUMUNKNOWN;

#if defined(__cplusplus) && !defined(CINTERFACE)
    DEFINE_GUID(IID_IEnumUnknown, 0x00000100, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
    struct IEnumUnknown : public IUnknown
    {
      public:
        virtual /* [local] */ HRESULT STDMETHODCALLTYPE Next(
            /* [annotation][in] */
            _In_ ULONG celt,
            /* [annotation][out] */
            _Out_writes_to_(celt, *pceltFetched) IUnknown **rgelt,
            /* [annotation][out] */
            _Out_opt_ ULONG *pceltFetched)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE Skip(
            /* [in] */ ULONG celt)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE Reset(void) = 0;

        virtual HRESULT STDMETHODCALLTYPE Clone(
            /* [out] */ __RPC__deref_out_opt IEnumUnknown **ppenum)
            = 0;
    };

#else /* C style interface */

    typedef struct IEnumUnknownVtbl
    {
        BEGIN_INTERFACE

        HRESULT(STDMETHODCALLTYPE *QueryInterface)
        (__RPC__in IEnumUnknown *This,
         /* [in] */ __RPC__in REFIID riid,
         /* [annotation][iid_is][out] */
         _COM_Outptr_ void **ppvObject);

        ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IEnumUnknown *This);

        ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IEnumUnknown *This);

        /* [local] */ HRESULT(STDMETHODCALLTYPE *Next)(IEnumUnknown *This,
                                                       /* [annotation][in] */
                                                       _In_ ULONG celt,
                                                       /* [annotation][out] */
                                                       _Out_writes_to_(celt, *pceltFetched) IUnknown **rgelt,
                                                       /* [annotation][out] */
                                                       _Out_opt_ ULONG *pceltFetched);

        HRESULT(STDMETHODCALLTYPE *Skip)
        (__RPC__in IEnumUnknown *This,
         /* [in] */ ULONG celt);

        HRESULT(STDMETHODCALLTYPE *Reset)(__RPC__in IEnumUnknown *This);

        HRESULT(STDMETHODCALLTYPE *Clone)
        (__RPC__in IEnumUnknown *This,
         /* [out] */ __RPC__deref_out_opt IEnumUnknown **ppenum);

        END_INTERFACE
    } IEnumUnknownVtbl;

    interface IEnumUnknown
    {
        CONST_VTBL struct IEnumUnknownVtbl *lpVtbl;
    };

#ifdef COBJMACROS

#define IEnumUnknown_QueryInterface(This, riid, ppvObject) ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IEnumUnknown_AddRef(This) ((This)->lpVtbl->AddRef(This))

#define IEnumUnknown_Release(This) ((This)->lpVtbl->Release(This))

#define IEnumUnknown_Next(This, celt, rgelt, pceltFetched) ((This)->lpVtbl->Next(This, celt, rgelt, pceltFetched))

#define IEnumUnknown_Skip(This, celt) ((This)->lpVtbl->Skip(This, celt))

#define IEnumUnknown_Reset(This) ((This)->lpVtbl->Reset(This))

#define IEnumUnknown_Clone(This, ppenum) ((This)->lpVtbl->Clone(This, ppenum))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IEnumUnknown_INTERFACE_DEFINED__ */

#ifndef __IOleContainer_INTERFACE_DEFINED__
#define __IOleContainer_INTERFACE_DEFINED__

    /* interface IOleContainer */
    /* [unique][uuid][object] */

    typedef /* [unique] */ __RPC_unique_pointer IOleContainer *LPOLECONTAINER;

#if defined(__cplusplus) && !defined(CINTERFACE)

    DEFINE_GUID(IID_IOleContainer, 0x0000011b, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
    struct IOleContainer : public IParseDisplayName
    {
      public:
        virtual HRESULT STDMETHODCALLTYPE EnumObjects(
            /* [in] */ DWORD grfFlags,
            /* [out] */ __RPC__deref_out_opt IEnumUnknown **ppenum)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE LockContainer(
            /* [in] */ BOOL fLock)
            = 0;
    };

#else /* C style interface */

    typedef struct IOleContainerVtbl
    {
        BEGIN_INTERFACE

        HRESULT(STDMETHODCALLTYPE *QueryInterface)
        (__RPC__in IOleContainer *This,
         /* [in] */ __RPC__in REFIID riid,
         /* [annotation][iid_is][out] */
         _COM_Outptr_ void **ppvObject);

        ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IOleContainer *This);

        ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IOleContainer *This);

        HRESULT(STDMETHODCALLTYPE *ParseDisplayName)
        (__RPC__in IOleContainer *This,
         /* [unique][in] */ __RPC__in_opt IBindCtx *pbc,
         /* [in] */ __RPC__in LPOLESTR pszDisplayName,
         /* [out] */ __RPC__out ULONG *pchEaten,
         /* [out] */ __RPC__deref_out_opt IMoniker **ppmkOut);

        HRESULT(STDMETHODCALLTYPE *EnumObjects)
        (__RPC__in IOleContainer *This,
         /* [in] */ DWORD grfFlags,
         /* [out] */ __RPC__deref_out_opt IEnumUnknown **ppenum);

        HRESULT(STDMETHODCALLTYPE *LockContainer)
        (__RPC__in IOleContainer *This,
         /* [in] */ BOOL fLock);

        END_INTERFACE
    } IOleContainerVtbl;

    interface IOleContainer
    {
        CONST_VTBL struct IOleContainerVtbl *lpVtbl;
    };

#ifdef COBJMACROS

#define IOleContainer_QueryInterface(This, riid, ppvObject) ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IOleContainer_AddRef(This) ((This)->lpVtbl->AddRef(This))

#define IOleContainer_Release(This) ((This)->lpVtbl->Release(This))

#define IOleContainer_ParseDisplayName(This, pbc, pszDisplayName, pchEaten, ppmkOut) ((This)->lpVtbl->ParseDisplayName(This, pbc, pszDisplayName, pchEaten, ppmkOut))

#define IOleContainer_EnumObjects(This, grfFlags, ppenum) ((This)->lpVtbl->EnumObjects(This, grfFlags, ppenum))

#define IOleContainer_LockContainer(This, fLock) ((This)->lpVtbl->LockContainer(This, fLock))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IOleContainer_INTERFACE_DEFINED__ */

#ifndef __IOleClientSite_INTERFACE_DEFINED__
#define __IOleClientSite_INTERFACE_DEFINED__

    /* interface IOleClientSite */
    /* [unique][uuid][object] */

    typedef /* [unique] */ __RPC_unique_pointer IOleClientSite *LPOLECLIENTSITE;

#if defined(__cplusplus) && !defined(CINTERFACE)
    DEFINE_GUID(IID_IOleClientSite, 0x00000118, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
    struct IOleClientSite : public IUnknown
    {
      public:
        virtual HRESULT STDMETHODCALLTYPE SaveObject(void) = 0;

        virtual HRESULT STDMETHODCALLTYPE GetMoniker(
            /* [in] */ DWORD dwAssign,
            /* [in] */ DWORD dwWhichMoniker,
            /* [out] */ __RPC__deref_out_opt IMoniker **ppmk)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE GetContainer(
            /* [out] */ __RPC__deref_out_opt IOleContainer **ppContainer)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE ShowObject(void) = 0;

        virtual HRESULT STDMETHODCALLTYPE OnShowWindow(
            /* [in] */ BOOL fShow)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE RequestNewObjectLayout(void) = 0;
    };

#else /* C style interface */

    typedef struct IOleClientSiteVtbl
    {
        BEGIN_INTERFACE

        HRESULT(STDMETHODCALLTYPE *QueryInterface)
        (__RPC__in IOleClientSite *This,
         /* [in] */ __RPC__in REFIID riid,
         /* [annotation][iid_is][out] */
         _COM_Outptr_ void **ppvObject);

        ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IOleClientSite *This);

        ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IOleClientSite *This);

        HRESULT(STDMETHODCALLTYPE *SaveObject)(__RPC__in IOleClientSite *This);

        HRESULT(STDMETHODCALLTYPE *GetMoniker)
        (__RPC__in IOleClientSite *This,
         /* [in] */ DWORD dwAssign,
         /* [in] */ DWORD dwWhichMoniker,
         /* [out] */ __RPC__deref_out_opt IMoniker **ppmk);

        HRESULT(STDMETHODCALLTYPE *GetContainer)
        (__RPC__in IOleClientSite *This,
         /* [out] */ __RPC__deref_out_opt IOleContainer **ppContainer);

        HRESULT(STDMETHODCALLTYPE *ShowObject)(__RPC__in IOleClientSite *This);

        HRESULT(STDMETHODCALLTYPE *OnShowWindow)
        (__RPC__in IOleClientSite *This,
         /* [in] */ BOOL fShow);

        HRESULT(STDMETHODCALLTYPE *RequestNewObjectLayout)(__RPC__in IOleClientSite *This);

        END_INTERFACE
    } IOleClientSiteVtbl;

    interface IOleClientSite
    {
        CONST_VTBL struct IOleClientSiteVtbl *lpVtbl;
    };

#ifdef COBJMACROS

#define IOleClientSite_QueryInterface(This, riid, ppvObject) ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IOleClientSite_AddRef(This) ((This)->lpVtbl->AddRef(This))

#define IOleClientSite_Release(This) ((This)->lpVtbl->Release(This))

#define IOleClientSite_SaveObject(This) ((This)->lpVtbl->SaveObject(This))

#define IOleClientSite_GetMoniker(This, dwAssign, dwWhichMoniker, ppmk) ((This)->lpVtbl->GetMoniker(This, dwAssign, dwWhichMoniker, ppmk))

#define IOleClientSite_GetContainer(This, ppContainer) ((This)->lpVtbl->GetContainer(This, ppContainer))

#define IOleClientSite_ShowObject(This) ((This)->lpVtbl->ShowObject(This))

#define IOleClientSite_OnShowWindow(This, fShow) ((This)->lpVtbl->OnShowWindow(This, fShow))

#define IOleClientSite_RequestNewObjectLayout(This) ((This)->lpVtbl->RequestNewObjectLayout(This))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IOleClientSite_INTERFACE_DEFINED__ */

#ifndef __IOleObject_INTERFACE_DEFINED__
#define __IOleObject_INTERFACE_DEFINED__
    /* interface IOleObject */
    /* [unique][uuid][object] */

    typedef /* [unique] */ __RPC_unique_pointer IOleObject *LPOLEOBJECT;

    typedef enum tagOLEGETMONIKER
    {
        OLEGETMONIKER_ONLYIFTHERE = 1,
        OLEGETMONIKER_FORCEASSIGN = 2,
        OLEGETMONIKER_UNASSIGN = 3,
        OLEGETMONIKER_TEMPFORUSER = 4
    } OLEGETMONIKER;

    typedef enum tagOLEWHICHMK
    {
        OLEWHICHMK_CONTAINER = 1,
        OLEWHICHMK_OBJREL = 2,
        OLEWHICHMK_OBJFULL = 3
    } OLEWHICHMK;

    typedef enum tagUSERCLASSTYPE
    {
        USERCLASSTYPE_FULL = 1,
        USERCLASSTYPE_SHORT = 2,
        USERCLASSTYPE_APPNAME = 3
    } USERCLASSTYPE;

    typedef enum tagOLEMISC
    {
        OLEMISC_RECOMPOSEONRESIZE = 0x1,
        OLEMISC_ONLYICONIC = 0x2,
        OLEMISC_INSERTNOTREPLACE = 0x4,
        OLEMISC_STATIC = 0x8,
        OLEMISC_CANTLINKINSIDE = 0x10,
        OLEMISC_CANLINKBYOLE1 = 0x20,
        OLEMISC_ISLINKOBJECT = 0x40,
        OLEMISC_INSIDEOUT = 0x80,
        OLEMISC_ACTIVATEWHENVISIBLE = 0x100,
        OLEMISC_RENDERINGISDEVICEINDEPENDENT = 0x200,
        OLEMISC_INVISIBLEATRUNTIME = 0x400,
        OLEMISC_ALWAYSRUN = 0x800,
        OLEMISC_ACTSLIKEBUTTON = 0x1000,
        OLEMISC_ACTSLIKELABEL = 0x2000,
        OLEMISC_NOUIACTIVATE = 0x4000,
        OLEMISC_ALIGNABLE = 0x8000,
        OLEMISC_SIMPLEFRAME = 0x10000,
        OLEMISC_SETCLIENTSITEFIRST = 0x20000,
        OLEMISC_IMEMODE = 0x40000,
        OLEMISC_IGNOREACTIVATEWHENVISIBLE = 0x80000,
        OLEMISC_WANTSTOMENUMERGE = 0x100000,
        OLEMISC_SUPPORTSMULTILEVELUNDO = 0x200000
    } OLEMISC;

    typedef enum tagOLECLOSE
    {
        OLECLOSE_SAVEIFDIRTY = 0,
        OLECLOSE_NOSAVE = 1,
        OLECLOSE_PROMPTSAVE = 2
    } OLECLOSE;

#if defined(__cplusplus) && !defined(CINTERFACE)

    DEFINE_GUID(IID_IOleObject, 0x00000112, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
    struct IOleObject : public IUnknown
    {
      public:
        virtual HRESULT STDMETHODCALLTYPE SetClientSite(
            /* [unique][in] */ __RPC__in_opt IOleClientSite *pClientSite)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE GetClientSite(
            /* [out] */ __RPC__deref_out_opt IOleClientSite **ppClientSite)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE SetHostNames(
            /* [in] */ __RPC__in LPCOLESTR szContainerApp,
            /* [unique][in] */ __RPC__in_opt LPCOLESTR szContainerObj)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE Close(
            /* [in] */ DWORD dwSaveOption)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE SetMoniker(
            /* [in] */ DWORD dwWhichMoniker,
            /* [unique][in] */ __RPC__in_opt IMoniker *pmk)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE GetMoniker(
            /* [in] */ DWORD dwAssign,
            /* [in] */ DWORD dwWhichMoniker,
            /* [out] */ __RPC__deref_out_opt IMoniker **ppmk)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE InitFromData(
            /* [unique][in] */ __RPC__in_opt IDataObject *pDataObject,
            /* [in] */ BOOL fCreation,
            /* [in] */ DWORD dwReserved)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE GetClipboardData(
            /* [in] */ DWORD dwReserved,
            /* [out] */ __RPC__deref_out_opt IDataObject **ppDataObject)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE DoVerb(
            /* [in] */ LONG iVerb,
            /* [unique][in] */ __RPC__in_opt LPMSG lpmsg,
            /* [unique][in] */ __RPC__in_opt IOleClientSite *pActiveSite,
            /* [in] */ LONG lindex,
            /* [in] */ __RPC__in HWND hwndParent,
            /* [unique][in] */ __RPC__in_opt LPCRECT lprcPosRect)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE EnumVerbs(
            /* [out] */ __RPC__deref_out_opt IEnumOLEVERB **ppEnumOleVerb)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE Update(void) = 0;

        virtual HRESULT STDMETHODCALLTYPE IsUpToDate(void) = 0;

        virtual HRESULT STDMETHODCALLTYPE GetUserClassID(
            /* [out] */ __RPC__out CLSID *pClsid)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE GetUserType(
            /* [in] */ DWORD dwFormOfType,
            /* [out] */ __RPC__deref_out_opt LPOLESTR *pszUserType)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE SetExtent(
            /* [in] */ DWORD dwDrawAspect,
            /* [in] */ __RPC__in SIZEL *psizel)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE GetExtent(
            /* [in] */ DWORD dwDrawAspect,
            /* [out] */ __RPC__out SIZEL *psizel)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE Advise(
            /* [unique][in] */ __RPC__in_opt IAdviseSink *pAdvSink,
            /* [out] */ __RPC__out DWORD *pdwConnection)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE Unadvise(
            /* [in] */ DWORD dwConnection)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE EnumAdvise(
            /* [out] */ __RPC__deref_out_opt IEnumSTATDATA **ppenumAdvise)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE GetMiscStatus(
            /* [in] */ DWORD dwAspect,
            /* [out] */ __RPC__out DWORD *pdwStatus)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE SetColorScheme(
            /* [in] */ __RPC__in LOGPALETTE *pLogpal)
            = 0;
    };

#else /* C style interface */

    typedef struct IOleObjectVtbl
    {
        BEGIN_INTERFACE

        HRESULT(STDMETHODCALLTYPE *QueryInterface)
        (__RPC__in IOleObject *This,
         /* [in] */ __RPC__in REFIID riid,
         /* [annotation][iid_is][out] */
         _COM_Outptr_ void **ppvObject);

        ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IOleObject *This);

        ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IOleObject *This);

        HRESULT(STDMETHODCALLTYPE *SetClientSite)
        (__RPC__in IOleObject *This,
         /* [unique][in] */ __RPC__in_opt IOleClientSite *pClientSite);

        HRESULT(STDMETHODCALLTYPE *GetClientSite)
        (__RPC__in IOleObject *This,
         /* [out] */ __RPC__deref_out_opt IOleClientSite **ppClientSite);

        HRESULT(STDMETHODCALLTYPE *SetHostNames)
        (__RPC__in IOleObject *This,
         /* [in] */ __RPC__in LPCOLESTR szContainerApp,
         /* [unique][in] */ __RPC__in_opt LPCOLESTR szContainerObj);

        HRESULT(STDMETHODCALLTYPE *Close)
        (__RPC__in IOleObject *This,
         /* [in] */ DWORD dwSaveOption);

        HRESULT(STDMETHODCALLTYPE *SetMoniker)
        (__RPC__in IOleObject *This,
         /* [in] */ DWORD dwWhichMoniker,
         /* [unique][in] */ __RPC__in_opt IMoniker *pmk);

        HRESULT(STDMETHODCALLTYPE *GetMoniker)
        (__RPC__in IOleObject *This,
         /* [in] */ DWORD dwAssign,
         /* [in] */ DWORD dwWhichMoniker,
         /* [out] */ __RPC__deref_out_opt IMoniker **ppmk);

        HRESULT(STDMETHODCALLTYPE *InitFromData)
        (__RPC__in IOleObject *This,
         /* [unique][in] */ __RPC__in_opt IDataObject *pDataObject,
         /* [in] */ BOOL fCreation,
         /* [in] */ DWORD dwReserved);

        HRESULT(STDMETHODCALLTYPE *GetClipboardData)
        (__RPC__in IOleObject *This,
         /* [in] */ DWORD dwReserved,
         /* [out] */ __RPC__deref_out_opt IDataObject **ppDataObject);

        HRESULT(STDMETHODCALLTYPE *DoVerb)
        (__RPC__in IOleObject *This,
         /* [in] */ LONG iVerb,
         /* [unique][in] */ __RPC__in_opt LPMSG lpmsg,
         /* [unique][in] */ __RPC__in_opt IOleClientSite *pActiveSite,
         /* [in] */ LONG lindex,
         /* [in] */ __RPC__in HWND hwndParent,
         /* [unique][in] */ __RPC__in_opt LPCRECT lprcPosRect);

        HRESULT(STDMETHODCALLTYPE *EnumVerbs)
        (__RPC__in IOleObject *This,
         /* [out] */ __RPC__deref_out_opt IEnumOLEVERB **ppEnumOleVerb);

        HRESULT(STDMETHODCALLTYPE *Update)(__RPC__in IOleObject *This);

        HRESULT(STDMETHODCALLTYPE *IsUpToDate)(__RPC__in IOleObject *This);

        HRESULT(STDMETHODCALLTYPE *GetUserClassID)
        (__RPC__in IOleObject *This,
         /* [out] */ __RPC__out CLSID *pClsid);

        HRESULT(STDMETHODCALLTYPE *GetUserType)
        (__RPC__in IOleObject *This,
         /* [in] */ DWORD dwFormOfType,
         /* [out] */ __RPC__deref_out_opt LPOLESTR *pszUserType);

        HRESULT(STDMETHODCALLTYPE *SetExtent)
        (__RPC__in IOleObject *This,
         /* [in] */ DWORD dwDrawAspect,
         /* [in] */ __RPC__in SIZEL *psizel);

        HRESULT(STDMETHODCALLTYPE *GetExtent)
        (__RPC__in IOleObject *This,
         /* [in] */ DWORD dwDrawAspect,
         /* [out] */ __RPC__out SIZEL *psizel);

        HRESULT(STDMETHODCALLTYPE *Advise)
        (__RPC__in IOleObject *This,
         /* [unique][in] */ __RPC__in_opt IAdviseSink *pAdvSink,
         /* [out] */ __RPC__out DWORD *pdwConnection);

        HRESULT(STDMETHODCALLTYPE *Unadvise)
        (__RPC__in IOleObject *This,
         /* [in] */ DWORD dwConnection);

        HRESULT(STDMETHODCALLTYPE *EnumAdvise)
        (__RPC__in IOleObject *This,
         /* [out] */ __RPC__deref_out_opt IEnumSTATDATA **ppenumAdvise);

        HRESULT(STDMETHODCALLTYPE *GetMiscStatus)
        (__RPC__in IOleObject *This,
         /* [in] */ DWORD dwAspect,
         /* [out] */ __RPC__out DWORD *pdwStatus);

        HRESULT(STDMETHODCALLTYPE *SetColorScheme)
        (__RPC__in IOleObject *This,
         /* [in] */ __RPC__in LOGPALETTE *pLogpal);

        END_INTERFACE
    } IOleObjectVtbl;

    interface IOleObject
    {
        CONST_VTBL struct IOleObjectVtbl *lpVtbl;
    };

#ifdef COBJMACROS

#define IOleObject_QueryInterface(This, riid, ppvObject) ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IOleObject_AddRef(This) ((This)->lpVtbl->AddRef(This))

#define IOleObject_Release(This) ((This)->lpVtbl->Release(This))

#define IOleObject_SetClientSite(This, pClientSite) ((This)->lpVtbl->SetClientSite(This, pClientSite))

#define IOleObject_GetClientSite(This, ppClientSite) ((This)->lpVtbl->GetClientSite(This, ppClientSite))

#define IOleObject_SetHostNames(This, szContainerApp, szContainerObj) ((This)->lpVtbl->SetHostNames(This, szContainerApp, szContainerObj))

#define IOleObject_Close(This, dwSaveOption) ((This)->lpVtbl->Close(This, dwSaveOption))

#define IOleObject_SetMoniker(This, dwWhichMoniker, pmk) ((This)->lpVtbl->SetMoniker(This, dwWhichMoniker, pmk))

#define IOleObject_GetMoniker(This, dwAssign, dwWhichMoniker, ppmk) ((This)->lpVtbl->GetMoniker(This, dwAssign, dwWhichMoniker, ppmk))

#define IOleObject_InitFromData(This, pDataObject, fCreation, dwReserved) ((This)->lpVtbl->InitFromData(This, pDataObject, fCreation, dwReserved))

#define IOleObject_GetClipboardData(This, dwReserved, ppDataObject) ((This)->lpVtbl->GetClipboardData(This, dwReserved, ppDataObject))

#define IOleObject_DoVerb(This, iVerb, lpmsg, pActiveSite, lindex, hwndParent, lprcPosRect) ((This)->lpVtbl->DoVerb(This, iVerb, lpmsg, pActiveSite, lindex, hwndParent, lprcPosRect))

#define IOleObject_EnumVerbs(This, ppEnumOleVerb) ((This)->lpVtbl->EnumVerbs(This, ppEnumOleVerb))

#define IOleObject_Update(This) ((This)->lpVtbl->Update(This))

#define IOleObject_IsUpToDate(This) ((This)->lpVtbl->IsUpToDate(This))

#define IOleObject_GetUserClassID(This, pClsid) ((This)->lpVtbl->GetUserClassID(This, pClsid))

#define IOleObject_GetUserType(This, dwFormOfType, pszUserType) ((This)->lpVtbl->GetUserType(This, dwFormOfType, pszUserType))

#define IOleObject_SetExtent(This, dwDrawAspect, psizel) ((This)->lpVtbl->SetExtent(This, dwDrawAspect, psizel))

#define IOleObject_GetExtent(This, dwDrawAspect, psizel) ((This)->lpVtbl->GetExtent(This, dwDrawAspect, psizel))

#define IOleObject_Advise(This, pAdvSink, pdwConnection) ((This)->lpVtbl->Advise(This, pAdvSink, pdwConnection))

#define IOleObject_Unadvise(This, dwConnection) ((This)->lpVtbl->Unadvise(This, dwConnection))

#define IOleObject_EnumAdvise(This, ppenumAdvise) ((This)->lpVtbl->EnumAdvise(This, ppenumAdvise))

#define IOleObject_GetMiscStatus(This, dwAspect, pdwStatus) ((This)->lpVtbl->GetMiscStatus(This, dwAspect, pdwStatus))

#define IOleObject_SetColorScheme(This, pLogpal) ((This)->lpVtbl->SetColorScheme(This, pLogpal))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IOleObject_INTERFACE_DEFINED__ */

#ifndef __IOLETypes_INTERFACE_DEFINED__
#define __IOLETypes_INTERFACE_DEFINED__

    /* interface IOLETypes */
    /* [uuid] */

    typedef enum tagOLERENDER
    {
        OLERENDER_NONE = 0,
        OLERENDER_DRAW = 1,
        OLERENDER_FORMAT = 2,
        OLERENDER_ASIS = 3
    } OLERENDER;

    typedef OLERENDER *LPOLERENDER;

    typedef struct tagOBJECTDESCRIPTOR
    {
        ULONG cbSize;
        CLSID clsid;
        DWORD dwDrawAspect;
        SIZEL sizel;
        POINTL pointl;
        DWORD dwStatus;
        DWORD dwFullUserTypeName;
        DWORD dwSrcOfCopy;
    } OBJECTDESCRIPTOR;

    typedef struct tagOBJECTDESCRIPTOR *POBJECTDESCRIPTOR;

    typedef struct tagOBJECTDESCRIPTOR *LPOBJECTDESCRIPTOR;

    typedef struct tagOBJECTDESCRIPTOR LINKSRCDESCRIPTOR;

    typedef struct tagOBJECTDESCRIPTOR *PLINKSRCDESCRIPTOR;

    typedef struct tagOBJECTDESCRIPTOR *LPLINKSRCDESCRIPTOR;

#endif /* __IOLETypes_INTERFACE_DEFINED__ */

#ifndef __IOleWindow_INTERFACE_DEFINED__
#define __IOleWindow_INTERFACE_DEFINED__

    /* interface IOleWindow */
    /* [unique][uuid][object] */

    typedef /* [unique] */ __RPC_unique_pointer IOleWindow *LPOLEWINDOW;

#if defined(__cplusplus) && !defined(CINTERFACE)

    DEFINE_GUID(IID_IOleWindow, 0x00000114, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
    struct IOleWindow : public IUnknown
    {
      public:
        virtual /* [input_sync] */ HRESULT STDMETHODCALLTYPE GetWindow(
            /* [out] */ __RPC__deref_out_opt HWND *phwnd)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(
            /* [in] */ BOOL fEnterMode)
            = 0;
    };

#else /* C style interface */

    typedef struct IOleWindowVtbl
    {
        BEGIN_INTERFACE

        HRESULT(STDMETHODCALLTYPE *QueryInterface)
        (__RPC__in IOleWindow *This,
         /* [in] */ __RPC__in REFIID riid,
         /* [annotation][iid_is][out] */
         _COM_Outptr_ void **ppvObject);

        ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IOleWindow *This);

        ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IOleWindow *This);

        /* [input_sync] */ HRESULT(STDMETHODCALLTYPE *GetWindow)(__RPC__in IOleWindow *This,
                                                                 /* [out] */ __RPC__deref_out_opt HWND *phwnd);

        HRESULT(STDMETHODCALLTYPE *ContextSensitiveHelp)
        (__RPC__in IOleWindow *This,
         /* [in] */ BOOL fEnterMode);

        END_INTERFACE
    } IOleWindowVtbl;

    interface IOleWindow
    {
        CONST_VTBL struct IOleWindowVtbl *lpVtbl;
    };

#ifdef COBJMACROS

#define IOleWindow_QueryInterface(This, riid, ppvObject) ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IOleWindow_AddRef(This) ((This)->lpVtbl->AddRef(This))

#define IOleWindow_Release(This) ((This)->lpVtbl->Release(This))

#define IOleWindow_GetWindow(This, phwnd) ((This)->lpVtbl->GetWindow(This, phwnd))

#define IOleWindow_ContextSensitiveHelp(This, fEnterMode) ((This)->lpVtbl->ContextSensitiveHelp(This, fEnterMode))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IOleWindow_INTERFACE_DEFINED__ */

#ifndef __IOleLink_INTERFACE_DEFINED__
#define __IOleLink_INTERFACE_DEFINED__

    /* interface IOleLink */
    /* [uuid][object] */

    typedef /* [unique] */ __RPC_unique_pointer IOleLink *LPOLELINK;

    typedef enum tagOLEUPDATE
    {
        OLEUPDATE_ALWAYS = 1,
        OLEUPDATE_ONCALL = 3
    } OLEUPDATE;

    typedef OLEUPDATE *LPOLEUPDATE;

    typedef OLEUPDATE *POLEUPDATE;

    typedef enum tagOLELINKBIND
    {
        OLELINKBIND_EVENIFCLASSDIFF = 1
    } OLELINKBIND;

#if defined(__cplusplus) && !defined(CINTERFACE)

    DEFINE_GUID(IID_IOleLink, 0x0000011d, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
    struct IOleLink : public IUnknown
    {
      public:
        virtual HRESULT STDMETHODCALLTYPE SetUpdateOptions(
            /* [in] */ DWORD dwUpdateOpt)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE GetUpdateOptions(
            /* [out] */ __RPC__out DWORD *pdwUpdateOpt)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE SetSourceMoniker(
            /* [unique][in] */ __RPC__in_opt IMoniker *pmk,
            /* [in] */ __RPC__in REFCLSID rclsid)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE GetSourceMoniker(
            /* [out] */ __RPC__deref_out_opt IMoniker **ppmk)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE SetSourceDisplayName(
            /* [in] */ __RPC__in LPCOLESTR pszStatusText)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE GetSourceDisplayName(
            /* [out] */ __RPC__deref_out_opt LPOLESTR *ppszDisplayName)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE BindToSource(
            /* [in] */ DWORD bindflags,
            /* [unique][in] */ __RPC__in_opt IBindCtx *pbc)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE BindIfRunning(void) = 0;

        virtual HRESULT STDMETHODCALLTYPE GetBoundSource(
            /* [out] */ __RPC__deref_out_opt IUnknown **ppunk)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE UnbindSource(void) = 0;

        virtual HRESULT STDMETHODCALLTYPE Update(
            /* [unique][in] */ __RPC__in_opt IBindCtx *pbc)
            = 0;
    };

#else /* C style interface */

    typedef struct IOleLinkVtbl
    {
        BEGIN_INTERFACE

        HRESULT(STDMETHODCALLTYPE *QueryInterface)
        (__RPC__in IOleLink *This,
         /* [in] */ __RPC__in REFIID riid,
         /* [annotation][iid_is][out] */
         _COM_Outptr_ void **ppvObject);

        ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IOleLink *This);

        ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IOleLink *This);

        HRESULT(STDMETHODCALLTYPE *SetUpdateOptions)
        (__RPC__in IOleLink *This,
         /* [in] */ DWORD dwUpdateOpt);

        HRESULT(STDMETHODCALLTYPE *GetUpdateOptions)
        (__RPC__in IOleLink *This,
         /* [out] */ __RPC__out DWORD *pdwUpdateOpt);

        HRESULT(STDMETHODCALLTYPE *SetSourceMoniker)
        (__RPC__in IOleLink *This,
         /* [unique][in] */ __RPC__in_opt IMoniker *pmk,
         /* [in] */ __RPC__in REFCLSID rclsid);

        HRESULT(STDMETHODCALLTYPE *GetSourceMoniker)
        (__RPC__in IOleLink *This,
         /* [out] */ __RPC__deref_out_opt IMoniker **ppmk);

        HRESULT(STDMETHODCALLTYPE *SetSourceDisplayName)
        (__RPC__in IOleLink *This,
         /* [in] */ __RPC__in LPCOLESTR pszStatusText);

        HRESULT(STDMETHODCALLTYPE *GetSourceDisplayName)
        (__RPC__in IOleLink *This,
         /* [out] */ __RPC__deref_out_opt LPOLESTR *ppszDisplayName);

        HRESULT(STDMETHODCALLTYPE *BindToSource)
        (__RPC__in IOleLink *This,
         /* [in] */ DWORD bindflags,
         /* [unique][in] */ __RPC__in_opt IBindCtx *pbc);

        HRESULT(STDMETHODCALLTYPE *BindIfRunning)(__RPC__in IOleLink *This);

        HRESULT(STDMETHODCALLTYPE *GetBoundSource)
        (__RPC__in IOleLink *This,
         /* [out] */ __RPC__deref_out_opt IUnknown **ppunk);

        HRESULT(STDMETHODCALLTYPE *UnbindSource)(__RPC__in IOleLink *This);

        HRESULT(STDMETHODCALLTYPE *Update)
        (__RPC__in IOleLink *This,
         /* [unique][in] */ __RPC__in_opt IBindCtx *pbc);

        END_INTERFACE
    } IOleLinkVtbl;

    interface IOleLink
    {
        CONST_VTBL struct IOleLinkVtbl *lpVtbl;
    };

#ifdef COBJMACROS

#define IOleLink_QueryInterface(This, riid, ppvObject) ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IOleLink_AddRef(This) ((This)->lpVtbl->AddRef(This))

#define IOleLink_Release(This) ((This)->lpVtbl->Release(This))

#define IOleLink_SetUpdateOptions(This, dwUpdateOpt) ((This)->lpVtbl->SetUpdateOptions(This, dwUpdateOpt))

#define IOleLink_GetUpdateOptions(This, pdwUpdateOpt) ((This)->lpVtbl->GetUpdateOptions(This, pdwUpdateOpt))

#define IOleLink_SetSourceMoniker(This, pmk, rclsid) ((This)->lpVtbl->SetSourceMoniker(This, pmk, rclsid))

#define IOleLink_GetSourceMoniker(This, ppmk) ((This)->lpVtbl->GetSourceMoniker(This, ppmk))

#define IOleLink_SetSourceDisplayName(This, pszStatusText) ((This)->lpVtbl->SetSourceDisplayName(This, pszStatusText))

#define IOleLink_GetSourceDisplayName(This, ppszDisplayName) ((This)->lpVtbl->GetSourceDisplayName(This, ppszDisplayName))

#define IOleLink_BindToSource(This, bindflags, pbc) ((This)->lpVtbl->BindToSource(This, bindflags, pbc))

#define IOleLink_BindIfRunning(This) ((This)->lpVtbl->BindIfRunning(This))

#define IOleLink_GetBoundSource(This, ppunk) ((This)->lpVtbl->GetBoundSource(This, ppunk))

#define IOleLink_UnbindSource(This) ((This)->lpVtbl->UnbindSource(This))

#define IOleLink_Update(This, pbc) ((This)->lpVtbl->Update(This, pbc))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IOleLink_INTERFACE_DEFINED__ */

#ifndef __IOleItemContainer_INTERFACE_DEFINED__
#define __IOleItemContainer_INTERFACE_DEFINED__

    /* interface IOleItemContainer */
    /* [unique][uuid][object] */

    typedef /* [unique] */ __RPC_unique_pointer IOleItemContainer *LPOLEITEMCONTAINER;

    typedef enum tagBINDSPEED
    {
        BINDSPEED_INDEFINITE = 1,
        BINDSPEED_MODERATE = 2,
        BINDSPEED_IMMEDIATE = 3
    } BINDSPEED;

    typedef /* [v1_enum] */
        enum tagOLECONTF
    {
        OLECONTF_EMBEDDINGS = 1,
        OLECONTF_LINKS = 2,
        OLECONTF_OTHERS = 4,
        OLECONTF_ONLYUSER = 8,
        OLECONTF_ONLYIFRUNNING = 16
    } OLECONTF;

    EXTERN_C const IID IID_IOleItemContainer;

#if defined(__cplusplus) && !defined(CINTERFACE)
    DEFINE_GUID(IID_IOleItemContainer, 0x0000011c, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
    struct IOleItemContainer : public IOleContainer
    {
      public:
        virtual HRESULT STDMETHODCALLTYPE GetObject(
            /* [in] */ __RPC__in LPOLESTR pszItem,
            /* [in] */ DWORD dwSpeedNeeded,
            /* [unique][in] */ __RPC__in_opt IBindCtx *pbc,
            /* [in] */ __RPC__in REFIID riid,
            /* [iid_is][out] */ __RPC__deref_out_opt void **ppvObject)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE GetObjectStorage(
            /* [in] */ __RPC__in LPOLESTR pszItem,
            /* [unique][in] */ __RPC__in_opt IBindCtx *pbc,
            /* [in] */ __RPC__in REFIID riid,
            /* [iid_is][out] */ __RPC__deref_out_opt void **ppvStorage)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE IsRunning(
            /* [in] */ __RPC__in LPOLESTR pszItem)
            = 0;
    };

#else /* C style interface */

    typedef struct IOleItemContainerVtbl
    {
        BEGIN_INTERFACE

        HRESULT(STDMETHODCALLTYPE *QueryInterface)
        (__RPC__in IOleItemContainer *This,
         /* [in] */ __RPC__in REFIID riid,
         /* [annotation][iid_is][out] */
         _COM_Outptr_ void **ppvObject);

        ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IOleItemContainer *This);

        ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IOleItemContainer *This);

        HRESULT(STDMETHODCALLTYPE *ParseDisplayName)
        (__RPC__in IOleItemContainer *This,
         /* [unique][in] */ __RPC__in_opt IBindCtx *pbc,
         /* [in] */ __RPC__in LPOLESTR pszDisplayName,
         /* [out] */ __RPC__out ULONG *pchEaten,
         /* [out] */ __RPC__deref_out_opt IMoniker **ppmkOut);

        HRESULT(STDMETHODCALLTYPE *EnumObjects)
        (__RPC__in IOleItemContainer *This,
         /* [in] */ DWORD grfFlags,
         /* [out] */ __RPC__deref_out_opt IEnumUnknown **ppenum);

        HRESULT(STDMETHODCALLTYPE *LockContainer)
        (__RPC__in IOleItemContainer *This,
         /* [in] */ BOOL fLock);

        HRESULT(STDMETHODCALLTYPE *GetObject)
        (__RPC__in IOleItemContainer *This,
         /* [in] */ __RPC__in LPOLESTR pszItem,
         /* [in] */ DWORD dwSpeedNeeded,
         /* [unique][in] */ __RPC__in_opt IBindCtx *pbc,
         /* [in] */ __RPC__in REFIID riid,
         /* [iid_is][out] */ __RPC__deref_out_opt void **ppvObject);

        HRESULT(STDMETHODCALLTYPE *GetObjectStorage)
        (__RPC__in IOleItemContainer *This,
         /* [in] */ __RPC__in LPOLESTR pszItem,
         /* [unique][in] */ __RPC__in_opt IBindCtx *pbc,
         /* [in] */ __RPC__in REFIID riid,
         /* [iid_is][out] */ __RPC__deref_out_opt void **ppvStorage);

        HRESULT(STDMETHODCALLTYPE *IsRunning)
        (__RPC__in IOleItemContainer *This,
         /* [in] */ __RPC__in LPOLESTR pszItem);

        END_INTERFACE
    } IOleItemContainerVtbl;

    interface IOleItemContainer
    {
        CONST_VTBL struct IOleItemContainerVtbl *lpVtbl;
    };

#ifdef COBJMACROS

#define IOleItemContainer_QueryInterface(This, riid, ppvObject) ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IOleItemContainer_AddRef(This) ((This)->lpVtbl->AddRef(This))

#define IOleItemContainer_Release(This) ((This)->lpVtbl->Release(This))

#define IOleItemContainer_ParseDisplayName(This, pbc, pszDisplayName, pchEaten, ppmkOut) ((This)->lpVtbl->ParseDisplayName(This, pbc, pszDisplayName, pchEaten, ppmkOut))

#define IOleItemContainer_EnumObjects(This, grfFlags, ppenum) ((This)->lpVtbl->EnumObjects(This, grfFlags, ppenum))

#define IOleItemContainer_LockContainer(This, fLock) ((This)->lpVtbl->LockContainer(This, fLock))

#define IOleItemContainer_GetObject(This, pszItem, dwSpeedNeeded, pbc, riid, ppvObject) ((This)->lpVtbl->GetObject(This, pszItem, dwSpeedNeeded, pbc, riid, ppvObject))

#define IOleItemContainer_GetObjectStorage(This, pszItem, pbc, riid, ppvStorage) ((This)->lpVtbl->GetObjectStorage(This, pszItem, pbc, riid, ppvStorage))

#define IOleItemContainer_IsRunning(This, pszItem) ((This)->lpVtbl->IsRunning(This, pszItem))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IOleItemContainer_INTERFACE_DEFINED__ */

#ifndef __IOleInPlaceUIWindow_INTERFACE_DEFINED__
#define __IOleInPlaceUIWindow_INTERFACE_DEFINED__

    /* interface IOleInPlaceUIWindow */
    /* [unique][uuid][object] */

    typedef /* [unique] */ __RPC_unique_pointer IOleInPlaceUIWindow *LPOLEINPLACEUIWINDOW;

    typedef RECT BORDERWIDTHS;

    typedef LPRECT LPBORDERWIDTHS;

    typedef LPCRECT LPCBORDERWIDTHS;

#if defined(__cplusplus) && !defined(CINTERFACE)
    DEFINE_GUID(IID_IOleInPlaceUIWindow, 0x00000115, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
    struct IOleInPlaceUIWindow : public IOleWindow
    {
      public:
        virtual /* [input_sync] */ HRESULT STDMETHODCALLTYPE GetBorder(
            /* [out] */ __RPC__out LPRECT lprectBorder)
            = 0;

        virtual /* [input_sync] */ HRESULT STDMETHODCALLTYPE RequestBorderSpace(
            /* [unique][in] */ __RPC__in_opt LPCBORDERWIDTHS pborderwidths)
            = 0;

        virtual /* [input_sync] */ HRESULT STDMETHODCALLTYPE SetBorderSpace(
            /* [unique][in] */ __RPC__in_opt LPCBORDERWIDTHS pborderwidths)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE SetActiveObject(
            /* [unique][in] */ __RPC__in_opt IOleInPlaceActiveObject *pActiveObject,
            /* [unique][string][in] */ __RPC__in_opt_string LPCOLESTR pszObjName)
            = 0;
    };

#else /* C style interface */

    typedef struct IOleInPlaceUIWindowVtbl
    {
        BEGIN_INTERFACE

        HRESULT(STDMETHODCALLTYPE *QueryInterface)
        (__RPC__in IOleInPlaceUIWindow *This,
         /* [in] */ __RPC__in REFIID riid,
         /* [annotation][iid_is][out] */
         _COM_Outptr_ void **ppvObject);

        ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IOleInPlaceUIWindow *This);

        ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IOleInPlaceUIWindow *This);

        /* [input_sync] */ HRESULT(STDMETHODCALLTYPE *GetWindow)(__RPC__in IOleInPlaceUIWindow *This,
                                                                 /* [out] */ __RPC__deref_out_opt HWND *phwnd);

        HRESULT(STDMETHODCALLTYPE *ContextSensitiveHelp)
        (__RPC__in IOleInPlaceUIWindow *This,
         /* [in] */ BOOL fEnterMode);

        /* [input_sync] */ HRESULT(STDMETHODCALLTYPE *GetBorder)(__RPC__in IOleInPlaceUIWindow *This,
                                                                 /* [out] */ __RPC__out LPRECT lprectBorder);

        /* [input_sync] */ HRESULT(STDMETHODCALLTYPE *RequestBorderSpace)(__RPC__in IOleInPlaceUIWindow *This,
                                                                          /* [unique][in] */ __RPC__in_opt LPCBORDERWIDTHS pborderwidths);

        /* [input_sync] */ HRESULT(STDMETHODCALLTYPE *SetBorderSpace)(__RPC__in IOleInPlaceUIWindow *This,
                                                                      /* [unique][in] */ __RPC__in_opt LPCBORDERWIDTHS pborderwidths);

        HRESULT(STDMETHODCALLTYPE *SetActiveObject)
        (__RPC__in IOleInPlaceUIWindow *This,
         /* [unique][in] */ __RPC__in_opt IOleInPlaceActiveObject *pActiveObject,
         /* [unique][string][in] */ __RPC__in_opt_string LPCOLESTR pszObjName);

        END_INTERFACE
    } IOleInPlaceUIWindowVtbl;

    interface IOleInPlaceUIWindow
    {
        CONST_VTBL struct IOleInPlaceUIWindowVtbl *lpVtbl;
    };

#ifdef COBJMACROS

#define IOleInPlaceUIWindow_QueryInterface(This, riid, ppvObject) ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IOleInPlaceUIWindow_AddRef(This) ((This)->lpVtbl->AddRef(This))

#define IOleInPlaceUIWindow_Release(This) ((This)->lpVtbl->Release(This))

#define IOleInPlaceUIWindow_GetWindow(This, phwnd) ((This)->lpVtbl->GetWindow(This, phwnd))

#define IOleInPlaceUIWindow_ContextSensitiveHelp(This, fEnterMode) ((This)->lpVtbl->ContextSensitiveHelp(This, fEnterMode))

#define IOleInPlaceUIWindow_GetBorder(This, lprectBorder) ((This)->lpVtbl->GetBorder(This, lprectBorder))

#define IOleInPlaceUIWindow_RequestBorderSpace(This, pborderwidths) ((This)->lpVtbl->RequestBorderSpace(This, pborderwidths))

#define IOleInPlaceUIWindow_SetBorderSpace(This, pborderwidths) ((This)->lpVtbl->SetBorderSpace(This, pborderwidths))

#define IOleInPlaceUIWindow_SetActiveObject(This, pActiveObject, pszObjName) ((This)->lpVtbl->SetActiveObject(This, pActiveObject, pszObjName))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IOleInPlaceUIWindow_INTERFACE_DEFINED__ */

#ifndef __IOleInPlaceActiveObject_INTERFACE_DEFINED__
#define __IOleInPlaceActiveObject_INTERFACE_DEFINED__

    /* interface IOleInPlaceActiveObject */
    /* [uuid][object] */

    typedef /* [unique] */ __RPC_unique_pointer IOleInPlaceActiveObject *LPOLEINPLACEACTIVEOBJECT;

    EXTERN_C const IID IID_IOleInPlaceActiveObject;

#if defined(__cplusplus) && !defined(CINTERFACE)

    DEFINE_GUID(IID_IOleInPlaceActiveObject, 0x00000117, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
    struct IOleInPlaceActiveObject : public IOleWindow
    {
      public:
        virtual /* [local] */ HRESULT STDMETHODCALLTYPE TranslateAccelerator(
            /* [annotation][in] */
            _In_opt_ LPMSG lpmsg)
            = 0;

        virtual /* [input_sync] */ HRESULT STDMETHODCALLTYPE OnFrameWindowActivate(
            /* [in] */ BOOL fActivate)
            = 0;

        virtual /* [input_sync] */ HRESULT STDMETHODCALLTYPE OnDocWindowActivate(
            /* [in] */ BOOL fActivate)
            = 0;

        virtual /* [local] */ HRESULT STDMETHODCALLTYPE ResizeBorder(
            /* [annotation][in] */
            _In_ LPCRECT prcBorder,
            /* [annotation][unique][in] */
            _In_ IOleInPlaceUIWindow *pUIWindow,
            /* [annotation][in] */
            _In_ BOOL fFrameWindow)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE EnableModeless(
            /* [in] */ BOOL fEnable)
            = 0;
    };

#else /* C style interface */

    typedef struct IOleInPlaceActiveObjectVtbl
    {
        BEGIN_INTERFACE

        HRESULT(STDMETHODCALLTYPE *QueryInterface)
        (__RPC__in IOleInPlaceActiveObject *This,
         /* [in] */ __RPC__in REFIID riid,
         /* [annotation][iid_is][out] */
         _COM_Outptr_ void **ppvObject);

        ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IOleInPlaceActiveObject *This);

        ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IOleInPlaceActiveObject *This);

        /* [input_sync] */ HRESULT(STDMETHODCALLTYPE *GetWindow)(__RPC__in IOleInPlaceActiveObject *This,
                                                                 /* [out] */ __RPC__deref_out_opt HWND *phwnd);

        HRESULT(STDMETHODCALLTYPE *ContextSensitiveHelp)
        (__RPC__in IOleInPlaceActiveObject *This,
         /* [in] */ BOOL fEnterMode);

        /* [local] */ HRESULT(STDMETHODCALLTYPE *TranslateAccelerator)(IOleInPlaceActiveObject *This,
                                                                       /* [annotation][in] */
                                                                       _In_opt_ LPMSG lpmsg);

        /* [input_sync] */ HRESULT(STDMETHODCALLTYPE *OnFrameWindowActivate)(__RPC__in IOleInPlaceActiveObject *This,
                                                                             /* [in] */ BOOL fActivate);

        /* [input_sync] */ HRESULT(STDMETHODCALLTYPE *OnDocWindowActivate)(__RPC__in IOleInPlaceActiveObject *This,
                                                                           /* [in] */ BOOL fActivate);

        /* [local] */ HRESULT(STDMETHODCALLTYPE *ResizeBorder)(IOleInPlaceActiveObject *This,
                                                               /* [annotation][in] */
                                                               _In_ LPCRECT prcBorder,
                                                               /* [annotation][unique][in] */
                                                               _In_ IOleInPlaceUIWindow *pUIWindow,
                                                               /* [annotation][in] */
                                                               _In_ BOOL fFrameWindow);

        HRESULT(STDMETHODCALLTYPE *EnableModeless)
        (__RPC__in IOleInPlaceActiveObject *This,
         /* [in] */ BOOL fEnable);

        END_INTERFACE
    } IOleInPlaceActiveObjectVtbl;

    interface IOleInPlaceActiveObject
    {
        CONST_VTBL struct IOleInPlaceActiveObjectVtbl *lpVtbl;
    };

#ifdef COBJMACROS

#define IOleInPlaceActiveObject_QueryInterface(This, riid, ppvObject) ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IOleInPlaceActiveObject_AddRef(This) ((This)->lpVtbl->AddRef(This))

#define IOleInPlaceActiveObject_Release(This) ((This)->lpVtbl->Release(This))

#define IOleInPlaceActiveObject_GetWindow(This, phwnd) ((This)->lpVtbl->GetWindow(This, phwnd))

#define IOleInPlaceActiveObject_ContextSensitiveHelp(This, fEnterMode) ((This)->lpVtbl->ContextSensitiveHelp(This, fEnterMode))

#define IOleInPlaceActiveObject_TranslateAccelerator(This, lpmsg) ((This)->lpVtbl->TranslateAccelerator(This, lpmsg))

#define IOleInPlaceActiveObject_OnFrameWindowActivate(This, fActivate) ((This)->lpVtbl->OnFrameWindowActivate(This, fActivate))

#define IOleInPlaceActiveObject_OnDocWindowActivate(This, fActivate) ((This)->lpVtbl->OnDocWindowActivate(This, fActivate))

#define IOleInPlaceActiveObject_ResizeBorder(This, prcBorder, pUIWindow, fFrameWindow) ((This)->lpVtbl->ResizeBorder(This, prcBorder, pUIWindow, fFrameWindow))

#define IOleInPlaceActiveObject_EnableModeless(This, fEnable) ((This)->lpVtbl->EnableModeless(This, fEnable))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IOleInPlaceActiveObject_INTERFACE_DEFINED__ */

#ifndef __IOleInPlaceFrame_INTERFACE_DEFINED__
#define __IOleInPlaceFrame_INTERFACE_DEFINED__

    /* interface IOleInPlaceFrame */
    /* [unique][uuid][object] */

    typedef /* [unique] */ __RPC_unique_pointer IOleInPlaceFrame *LPOLEINPLACEFRAME;

    typedef struct tagOIFI
    {
        UINT cb;
        BOOL fMDIApp;
        HWND hwndFrame;
        HACCEL haccel;
        UINT cAccelEntries;
    } OLEINPLACEFRAMEINFO;

    typedef struct tagOIFI *LPOLEINPLACEFRAMEINFO;

    typedef struct tagOleMenuGroupWidths
    {
        LONG width[6];
    } OLEMENUGROUPWIDTHS;

    typedef struct tagOleMenuGroupWidths *LPOLEMENUGROUPWIDTHS;

    typedef HGLOBAL HOLEMENU;

#if defined(__cplusplus) && !defined(CINTERFACE)

    DEFINE_GUID(IID_IOleInPlaceFrame, 0x00000116, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
    struct IOleInPlaceFrame : public IOleInPlaceUIWindow
    {
      public:
        virtual HRESULT STDMETHODCALLTYPE InsertMenus(
            /* [in] */ __RPC__in HMENU hmenuShared,
            /* [out][in] */ __RPC__inout LPOLEMENUGROUPWIDTHS lpMenuWidths)
            = 0;

        virtual /* [input_sync] */ HRESULT STDMETHODCALLTYPE SetMenu(
            /* [in] */ __RPC__in HMENU hmenuShared,
            /* [in] */ __RPC__in HOLEMENU holemenu,
            /* [in] */ __RPC__in HWND hwndActiveObject)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE RemoveMenus(
            /* [in] */ __RPC__in HMENU hmenuShared)
            = 0;

        virtual /* [input_sync] */ HRESULT STDMETHODCALLTYPE SetStatusText(
            /* [unique][in] */ __RPC__in_opt LPCOLESTR pszStatusText)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE EnableModeless(
            /* [in] */ BOOL fEnable)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE TranslateAccelerator(
            /* [in] */ __RPC__in LPMSG lpmsg,
            /* [in] */ WORD wID)
            = 0;
    };

#else /* C style interface */

    typedef struct IOleInPlaceFrameVtbl
    {
        BEGIN_INTERFACE

        HRESULT(STDMETHODCALLTYPE *QueryInterface)
        (__RPC__in IOleInPlaceFrame *This,
         /* [in] */ __RPC__in REFIID riid,
         /* [annotation][iid_is][out] */
         _COM_Outptr_ void **ppvObject);

        ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IOleInPlaceFrame *This);

        ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IOleInPlaceFrame *This);

        /* [input_sync] */ HRESULT(STDMETHODCALLTYPE *GetWindow)(__RPC__in IOleInPlaceFrame *This,
                                                                 /* [out] */ __RPC__deref_out_opt HWND *phwnd);

        HRESULT(STDMETHODCALLTYPE *ContextSensitiveHelp)
        (__RPC__in IOleInPlaceFrame *This,
         /* [in] */ BOOL fEnterMode);

        /* [input_sync] */ HRESULT(STDMETHODCALLTYPE *GetBorder)(__RPC__in IOleInPlaceFrame *This,
                                                                 /* [out] */ __RPC__out LPRECT lprectBorder);

        /* [input_sync] */ HRESULT(STDMETHODCALLTYPE *RequestBorderSpace)(__RPC__in IOleInPlaceFrame *This,
                                                                          /* [unique][in] */ __RPC__in_opt LPCBORDERWIDTHS pborderwidths);

        /* [input_sync] */ HRESULT(STDMETHODCALLTYPE *SetBorderSpace)(__RPC__in IOleInPlaceFrame *This,
                                                                      /* [unique][in] */ __RPC__in_opt LPCBORDERWIDTHS pborderwidths);

        HRESULT(STDMETHODCALLTYPE *SetActiveObject)
        (__RPC__in IOleInPlaceFrame *This,
         /* [unique][in] */ __RPC__in_opt IOleInPlaceActiveObject *pActiveObject,
         /* [unique][string][in] */ __RPC__in_opt_string LPCOLESTR pszObjName);

        HRESULT(STDMETHODCALLTYPE *InsertMenus)
        (__RPC__in IOleInPlaceFrame *This,
         /* [in] */ __RPC__in HMENU hmenuShared,
         /* [out][in] */ __RPC__inout LPOLEMENUGROUPWIDTHS lpMenuWidths);

        /* [input_sync] */ HRESULT(STDMETHODCALLTYPE *SetMenu)(__RPC__in IOleInPlaceFrame *This,
                                                               /* [in] */ __RPC__in HMENU hmenuShared,
                                                               /* [in] */ __RPC__in HOLEMENU holemenu,
                                                               /* [in] */ __RPC__in HWND hwndActiveObject);

        HRESULT(STDMETHODCALLTYPE *RemoveMenus)
        (__RPC__in IOleInPlaceFrame *This,
         /* [in] */ __RPC__in HMENU hmenuShared);

        /* [input_sync] */ HRESULT(STDMETHODCALLTYPE *SetStatusText)(__RPC__in IOleInPlaceFrame *This,
                                                                     /* [unique][in] */ __RPC__in_opt LPCOLESTR pszStatusText);

        HRESULT(STDMETHODCALLTYPE *EnableModeless)
        (__RPC__in IOleInPlaceFrame *This,
         /* [in] */ BOOL fEnable);

        HRESULT(STDMETHODCALLTYPE *TranslateAccelerator)
        (__RPC__in IOleInPlaceFrame *This,
         /* [in] */ __RPC__in LPMSG lpmsg,
         /* [in] */ WORD wID);

        END_INTERFACE
    } IOleInPlaceFrameVtbl;

    interface IOleInPlaceFrame
    {
        CONST_VTBL struct IOleInPlaceFrameVtbl *lpVtbl;
    };

#ifdef COBJMACROS

#define IOleInPlaceFrame_QueryInterface(This, riid, ppvObject) ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IOleInPlaceFrame_AddRef(This) ((This)->lpVtbl->AddRef(This))

#define IOleInPlaceFrame_Release(This) ((This)->lpVtbl->Release(This))

#define IOleInPlaceFrame_GetWindow(This, phwnd) ((This)->lpVtbl->GetWindow(This, phwnd))

#define IOleInPlaceFrame_ContextSensitiveHelp(This, fEnterMode) ((This)->lpVtbl->ContextSensitiveHelp(This, fEnterMode))

#define IOleInPlaceFrame_GetBorder(This, lprectBorder) ((This)->lpVtbl->GetBorder(This, lprectBorder))

#define IOleInPlaceFrame_RequestBorderSpace(This, pborderwidths) ((This)->lpVtbl->RequestBorderSpace(This, pborderwidths))

#define IOleInPlaceFrame_SetBorderSpace(This, pborderwidths) ((This)->lpVtbl->SetBorderSpace(This, pborderwidths))

#define IOleInPlaceFrame_SetActiveObject(This, pActiveObject, pszObjName) ((This)->lpVtbl->SetActiveObject(This, pActiveObject, pszObjName))

#define IOleInPlaceFrame_InsertMenus(This, hmenuShared, lpMenuWidths) ((This)->lpVtbl->InsertMenus(This, hmenuShared, lpMenuWidths))

#define IOleInPlaceFrame_SetMenu(This, hmenuShared, holemenu, hwndActiveObject) ((This)->lpVtbl->SetMenu(This, hmenuShared, holemenu, hwndActiveObject))

#define IOleInPlaceFrame_RemoveMenus(This, hmenuShared) ((This)->lpVtbl->RemoveMenus(This, hmenuShared))

#define IOleInPlaceFrame_SetStatusText(This, pszStatusText) ((This)->lpVtbl->SetStatusText(This, pszStatusText))

#define IOleInPlaceFrame_EnableModeless(This, fEnable) ((This)->lpVtbl->EnableModeless(This, fEnable))

#define IOleInPlaceFrame_TranslateAccelerator(This, lpmsg, wID) ((This)->lpVtbl->TranslateAccelerator(This, lpmsg, wID))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IOleInPlaceFrame_INTERFACE_DEFINED__ */

#ifndef __IOleInPlaceObject_INTERFACE_DEFINED__
#define __IOleInPlaceObject_INTERFACE_DEFINED__

    /* interface IOleInPlaceObject */
    /* [unique][uuid][object] */

    typedef /* [unique] */ __RPC_unique_pointer IOleInPlaceObject *LPOLEINPLACEOBJECT;

#if defined(__cplusplus) && !defined(CINTERFACE)
    DEFINE_GUID(IID_IOleInPlaceObject, 0x00000113, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
    struct IOleInPlaceObject : public IOleWindow
    {
      public:
        virtual HRESULT STDMETHODCALLTYPE InPlaceDeactivate(void) = 0;

        virtual HRESULT STDMETHODCALLTYPE UIDeactivate(void) = 0;

        virtual /* [input_sync] */ HRESULT STDMETHODCALLTYPE SetObjectRects(
            /* [in] */ __RPC__in LPCRECT lprcPosRect,
            /* [in] */ __RPC__in LPCRECT lprcClipRect)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE ReactivateAndUndo(void) = 0;
    };

#else /* C style interface */

    typedef struct IOleInPlaceObjectVtbl
    {
        BEGIN_INTERFACE

        HRESULT(STDMETHODCALLTYPE *QueryInterface)
        (__RPC__in IOleInPlaceObject *This,
         /* [in] */ __RPC__in REFIID riid,
         /* [annotation][iid_is][out] */
         _COM_Outptr_ void **ppvObject);

        ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IOleInPlaceObject *This);

        ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IOleInPlaceObject *This);

        /* [input_sync] */ HRESULT(STDMETHODCALLTYPE *GetWindow)(__RPC__in IOleInPlaceObject *This,
                                                                 /* [out] */ __RPC__deref_out_opt HWND *phwnd);

        HRESULT(STDMETHODCALLTYPE *ContextSensitiveHelp)
        (__RPC__in IOleInPlaceObject *This,
         /* [in] */ BOOL fEnterMode);

        HRESULT(STDMETHODCALLTYPE *InPlaceDeactivate)(__RPC__in IOleInPlaceObject *This);

        HRESULT(STDMETHODCALLTYPE *UIDeactivate)(__RPC__in IOleInPlaceObject *This);

        /* [input_sync] */ HRESULT(STDMETHODCALLTYPE *SetObjectRects)(__RPC__in IOleInPlaceObject *This,
                                                                      /* [in] */ __RPC__in LPCRECT lprcPosRect,
                                                                      /* [in] */ __RPC__in LPCRECT lprcClipRect);

        HRESULT(STDMETHODCALLTYPE *ReactivateAndUndo)(__RPC__in IOleInPlaceObject *This);

        END_INTERFACE
    } IOleInPlaceObjectVtbl;

    interface IOleInPlaceObject
    {
        CONST_VTBL struct IOleInPlaceObjectVtbl *lpVtbl;
    };

#ifdef COBJMACROS

#define IOleInPlaceObject_QueryInterface(This, riid, ppvObject) ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IOleInPlaceObject_AddRef(This) ((This)->lpVtbl->AddRef(This))

#define IOleInPlaceObject_Release(This) ((This)->lpVtbl->Release(This))

#define IOleInPlaceObject_GetWindow(This, phwnd) ((This)->lpVtbl->GetWindow(This, phwnd))

#define IOleInPlaceObject_ContextSensitiveHelp(This, fEnterMode) ((This)->lpVtbl->ContextSensitiveHelp(This, fEnterMode))

#define IOleInPlaceObject_InPlaceDeactivate(This) ((This)->lpVtbl->InPlaceDeactivate(This))

#define IOleInPlaceObject_UIDeactivate(This) ((This)->lpVtbl->UIDeactivate(This))

#define IOleInPlaceObject_SetObjectRects(This, lprcPosRect, lprcClipRect) ((This)->lpVtbl->SetObjectRects(This, lprcPosRect, lprcClipRect))

#define IOleInPlaceObject_ReactivateAndUndo(This) ((This)->lpVtbl->ReactivateAndUndo(This))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IOleInPlaceObject_INTERFACE_DEFINED__ */

#ifndef __IOleInPlaceSite_INTERFACE_DEFINED__
#define __IOleInPlaceSite_INTERFACE_DEFINED__

    /* interface IOleInPlaceSite */
    /* [unique][uuid][object] */

    typedef /* [unique] */ __RPC_unique_pointer IOleInPlaceSite *LPOLEINPLACESITE;

#if defined(__cplusplus) && !defined(CINTERFACE)

    DEFINE_GUID(IID_IOleInPlaceSite, 0x00000119, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
    struct IOleInPlaceSite : public IOleWindow
    {
      public:
        virtual HRESULT STDMETHODCALLTYPE CanInPlaceActivate(void) = 0;

        virtual HRESULT STDMETHODCALLTYPE OnInPlaceActivate(void) = 0;

        virtual HRESULT STDMETHODCALLTYPE OnUIActivate(void) = 0;

        virtual HRESULT STDMETHODCALLTYPE GetWindowContext(
            /* [out] */ __RPC__deref_out_opt IOleInPlaceFrame **ppFrame,
            /* [out] */ __RPC__deref_out_opt IOleInPlaceUIWindow **ppDoc,
            /* [out] */ __RPC__out LPRECT lprcPosRect,
            /* [out] */ __RPC__out LPRECT lprcClipRect,
            /* [out][in] */ __RPC__inout LPOLEINPLACEFRAMEINFO lpFrameInfo)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE Scroll(
            /* [in] */ SIZE scrollExtant)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE OnUIDeactivate(
            /* [in] */ BOOL fUndoable)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE OnInPlaceDeactivate(void) = 0;

        virtual HRESULT STDMETHODCALLTYPE DiscardUndoState(void) = 0;

        virtual HRESULT STDMETHODCALLTYPE DeactivateAndUndo(void) = 0;

        virtual HRESULT STDMETHODCALLTYPE OnPosRectChange(
            /* [in] */ __RPC__in LPCRECT lprcPosRect)
            = 0;
    };

#else /* C style interface */

    typedef struct IOleInPlaceSiteVtbl
    {
        BEGIN_INTERFACE

        HRESULT(STDMETHODCALLTYPE *QueryInterface)
        (__RPC__in IOleInPlaceSite *This,
         /* [in] */ __RPC__in REFIID riid,
         /* [annotation][iid_is][out] */
         _COM_Outptr_ void **ppvObject);

        ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IOleInPlaceSite *This);

        ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IOleInPlaceSite *This);

        /* [input_sync] */ HRESULT(STDMETHODCALLTYPE *GetWindow)(__RPC__in IOleInPlaceSite *This,
                                                                 /* [out] */ __RPC__deref_out_opt HWND *phwnd);

        HRESULT(STDMETHODCALLTYPE *ContextSensitiveHelp)
        (__RPC__in IOleInPlaceSite *This,
         /* [in] */ BOOL fEnterMode);

        HRESULT(STDMETHODCALLTYPE *CanInPlaceActivate)(__RPC__in IOleInPlaceSite *This);

        HRESULT(STDMETHODCALLTYPE *OnInPlaceActivate)(__RPC__in IOleInPlaceSite *This);

        HRESULT(STDMETHODCALLTYPE *OnUIActivate)(__RPC__in IOleInPlaceSite *This);

        HRESULT(STDMETHODCALLTYPE *GetWindowContext)
        (__RPC__in IOleInPlaceSite *This,
         /* [out] */ __RPC__deref_out_opt IOleInPlaceFrame **ppFrame,
         /* [out] */ __RPC__deref_out_opt IOleInPlaceUIWindow **ppDoc,
         /* [out] */ __RPC__out LPRECT lprcPosRect,
         /* [out] */ __RPC__out LPRECT lprcClipRect,
         /* [out][in] */ __RPC__inout LPOLEINPLACEFRAMEINFO lpFrameInfo);

        HRESULT(STDMETHODCALLTYPE *Scroll)
        (__RPC__in IOleInPlaceSite *This,
         /* [in] */ SIZE scrollExtant);

        HRESULT(STDMETHODCALLTYPE *OnUIDeactivate)
        (__RPC__in IOleInPlaceSite *This,
         /* [in] */ BOOL fUndoable);

        HRESULT(STDMETHODCALLTYPE *OnInPlaceDeactivate)(__RPC__in IOleInPlaceSite *This);

        HRESULT(STDMETHODCALLTYPE *DiscardUndoState)(__RPC__in IOleInPlaceSite *This);

        HRESULT(STDMETHODCALLTYPE *DeactivateAndUndo)(__RPC__in IOleInPlaceSite *This);

        HRESULT(STDMETHODCALLTYPE *OnPosRectChange)
        (__RPC__in IOleInPlaceSite *This,
         /* [in] */ __RPC__in LPCRECT lprcPosRect);

        END_INTERFACE
    } IOleInPlaceSiteVtbl;

    interface IOleInPlaceSite
    {
        CONST_VTBL struct IOleInPlaceSiteVtbl *lpVtbl;
    };

#ifdef COBJMACROS

#define IOleInPlaceSite_QueryInterface(This, riid, ppvObject) ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IOleInPlaceSite_AddRef(This) ((This)->lpVtbl->AddRef(This))

#define IOleInPlaceSite_Release(This) ((This)->lpVtbl->Release(This))

#define IOleInPlaceSite_GetWindow(This, phwnd) ((This)->lpVtbl->GetWindow(This, phwnd))

#define IOleInPlaceSite_ContextSensitiveHelp(This, fEnterMode) ((This)->lpVtbl->ContextSensitiveHelp(This, fEnterMode))

#define IOleInPlaceSite_CanInPlaceActivate(This) ((This)->lpVtbl->CanInPlaceActivate(This))

#define IOleInPlaceSite_OnInPlaceActivate(This) ((This)->lpVtbl->OnInPlaceActivate(This))

#define IOleInPlaceSite_OnUIActivate(This) ((This)->lpVtbl->OnUIActivate(This))

#define IOleInPlaceSite_GetWindowContext(This, ppFrame, ppDoc, lprcPosRect, lprcClipRect, lpFrameInfo) ((This)->lpVtbl->GetWindowContext(This, ppFrame, ppDoc, lprcPosRect, lprcClipRect, lpFrameInfo))

#define IOleInPlaceSite_Scroll(This, scrollExtant) ((This)->lpVtbl->Scroll(This, scrollExtant))

#define IOleInPlaceSite_OnUIDeactivate(This, fUndoable) ((This)->lpVtbl->OnUIDeactivate(This, fUndoable))

#define IOleInPlaceSite_OnInPlaceDeactivate(This) ((This)->lpVtbl->OnInPlaceDeactivate(This))

#define IOleInPlaceSite_DiscardUndoState(This) ((This)->lpVtbl->DiscardUndoState(This))

#define IOleInPlaceSite_DeactivateAndUndo(This) ((This)->lpVtbl->DeactivateAndUndo(This))

#define IOleInPlaceSite_OnPosRectChange(This, lprcPosRect) ((This)->lpVtbl->OnPosRectChange(This, lprcPosRect))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IOleInPlaceSite_INTERFACE_DEFINED__ */

#ifndef __IContinue_INTERFACE_DEFINED__
#define __IContinue_INTERFACE_DEFINED__

    /* interface IContinue */
    /* [uuid][object] */

#if defined(__cplusplus) && !defined(CINTERFACE)

    DEFINE_GUID(IID_IContinue, 0x0000012a, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
    struct IContinue : public IUnknown
    {
      public:
        virtual HRESULT STDMETHODCALLTYPE FContinue(void) = 0;
    };

#else /* C style interface */

    typedef struct IContinueVtbl
    {
        BEGIN_INTERFACE

        HRESULT(STDMETHODCALLTYPE *QueryInterface)
        (__RPC__in IContinue *This,
         /* [in] */ __RPC__in REFIID riid,
         /* [annotation][iid_is][out] */
         _COM_Outptr_ void **ppvObject);

        ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IContinue *This);

        ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IContinue *This);

        HRESULT(STDMETHODCALLTYPE *FContinue)(__RPC__in IContinue *This);

        END_INTERFACE
    } IContinueVtbl;

    interface IContinue
    {
        CONST_VTBL struct IContinueVtbl *lpVtbl;
    };

#ifdef COBJMACROS

#define IContinue_QueryInterface(This, riid, ppvObject) ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IContinue_AddRef(This) ((This)->lpVtbl->AddRef(This))

#define IContinue_Release(This) ((This)->lpVtbl->Release(This))

#define IContinue_FContinue(This) ((This)->lpVtbl->FContinue(This))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IContinue_INTERFACE_DEFINED__ */

#ifndef __IViewObject_INTERFACE_DEFINED__
#define __IViewObject_INTERFACE_DEFINED__

    /* interface IViewObject */
    /* [uuid][object] */

    typedef /* [unique] */ __RPC_unique_pointer IViewObject *LPVIEWOBJECT;

#if defined(__cplusplus) && !defined(CINTERFACE)

    DEFINE_GUID(IID_IViewObject, 0x0000010d, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
    struct IViewObject : public IUnknown
    {
      public:
        virtual /* [local] */ HRESULT STDMETHODCALLTYPE Draw(
            /* [annotation][in] */
            _In_ DWORD dwDrawAspect,
            /* [annotation][in] */
            _In_ LONG lindex,
            /* [annotation][unique][in] */
            _Null_ void *pvAspect,
            /* [annotation][unique][in] */
            _In_opt_ DVTARGETDEVICE *ptd,
            /* [annotation][in] */
            _In_opt_ HDC hdcTargetDev,
            /* [annotation][in] */
            _In_ HDC hdcDraw,
            /* [annotation][in] */
            _In_opt_ LPCRECTL lprcBounds,
            /* [annotation][unique][in] */
            _In_opt_ LPCRECTL lprcWBounds,
            /* [annotation][in] */
            _In_opt_ BOOL(STDMETHODCALLTYPE *pfnContinue)(ULONG_PTR dwContinue),
            /* [annotation][in] */
            _In_ ULONG_PTR dwContinue)
            = 0;

        virtual /* [local] */ HRESULT STDMETHODCALLTYPE GetColorSet(
            /* [annotation][in] */
            _In_ DWORD dwDrawAspect,
            /* [annotation][in] */
            _In_ LONG lindex,
            /* [annotation][unique][in] */
            _Null_ void *pvAspect,
            /* [annotation][unique][in] */
            _In_opt_ DVTARGETDEVICE *ptd,
            /* [annotation][in] */
            _In_opt_ HDC hicTargetDev,
            /* [annotation][out] */
            _Outptr_ LOGPALETTE **ppColorSet)
            = 0;

        virtual /* [local] */ HRESULT STDMETHODCALLTYPE Freeze(
            /* [annotation][in] */
            _In_ DWORD dwDrawAspect,
            /* [annotation][in] */
            _In_ LONG lindex,
            /* [annotation][unique][in] */
            _Null_ void *pvAspect,
            /* [annotation][out] */
            _Out_ DWORD *pdwFreeze)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE Unfreeze(
            /* [in] */ DWORD dwFreeze)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE SetAdvise(
            /* [in] */ DWORD aspects,
            /* [in] */ DWORD advf,
            /* [unique][in] */ __RPC__in_opt IAdviseSink *pAdvSink)
            = 0;

        virtual /* [local] */ HRESULT STDMETHODCALLTYPE GetAdvise(
            /* [annotation][unique][out] */
            _Out_opt_ DWORD *pAspects,
            /* [annotation][unique][out] */
            _Out_opt_ DWORD *pAdvf,
            /* [annotation][out] */
            _Outptr_ IAdviseSink **ppAdvSink)
            = 0;
    };

#else /* C style interface */

    typedef struct IViewObjectVtbl
    {
        BEGIN_INTERFACE

        HRESULT(STDMETHODCALLTYPE *QueryInterface)
        (__RPC__in IViewObject *This,
         /* [in] */ __RPC__in REFIID riid,
         /* [annotation][iid_is][out] */
         _COM_Outptr_ void **ppvObject);

        ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IViewObject *This);

        ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IViewObject *This);

        /* [local] */ HRESULT(STDMETHODCALLTYPE *Draw)(IViewObject *This,
                                                       /* [annotation][in] */
                                                       _In_ DWORD dwDrawAspect,
                                                       /* [annotation][in] */
                                                       _In_ LONG lindex,
                                                       /* [annotation][unique][in] */
                                                       _Null_ void *pvAspect,
                                                       /* [annotation][unique][in] */
                                                       _In_opt_ DVTARGETDEVICE *ptd,
                                                       /* [annotation][in] */
                                                       _In_opt_ HDC hdcTargetDev,
                                                       /* [annotation][in] */
                                                       _In_ HDC hdcDraw,
                                                       /* [annotation][in] */
                                                       _In_opt_ LPCRECTL lprcBounds,
                                                       /* [annotation][unique][in] */
                                                       _In_opt_ LPCRECTL lprcWBounds,
                                                       /* [annotation][in] */
                                                       _In_opt_ BOOL(STDMETHODCALLTYPE *pfnContinue)(ULONG_PTR dwContinue),
                                                       /* [annotation][in] */
                                                       _In_ ULONG_PTR dwContinue);

        /* [local] */ HRESULT(STDMETHODCALLTYPE *GetColorSet)(IViewObject *This,
                                                              /* [annotation][in] */
                                                              _In_ DWORD dwDrawAspect,
                                                              /* [annotation][in] */
                                                              _In_ LONG lindex,
                                                              /* [annotation][unique][in] */
                                                              _Null_ void *pvAspect,
                                                              /* [annotation][unique][in] */
                                                              _In_opt_ DVTARGETDEVICE *ptd,
                                                              /* [annotation][in] */
                                                              _In_opt_ HDC hicTargetDev,
                                                              /* [annotation][out] */
                                                              _Outptr_ LOGPALETTE **ppColorSet);

        /* [local] */ HRESULT(STDMETHODCALLTYPE *Freeze)(IViewObject *This,
                                                         /* [annotation][in] */
                                                         _In_ DWORD dwDrawAspect,
                                                         /* [annotation][in] */
                                                         _In_ LONG lindex,
                                                         /* [annotation][unique][in] */
                                                         _Null_ void *pvAspect,
                                                         /* [annotation][out] */
                                                         _Out_ DWORD *pdwFreeze);

        HRESULT(STDMETHODCALLTYPE *Unfreeze)
        (__RPC__in IViewObject *This,
         /* [in] */ DWORD dwFreeze);

        HRESULT(STDMETHODCALLTYPE *SetAdvise)
        (__RPC__in IViewObject *This,
         /* [in] */ DWORD aspects,
         /* [in] */ DWORD advf,
         /* [unique][in] */ __RPC__in_opt IAdviseSink *pAdvSink);

        /* [local] */ HRESULT(STDMETHODCALLTYPE *GetAdvise)(IViewObject *This,
                                                            /* [annotation][unique][out] */
                                                            _Out_opt_ DWORD *pAspects,
                                                            /* [annotation][unique][out] */
                                                            _Out_opt_ DWORD *pAdvf,
                                                            /* [annotation][out] */
                                                            _Outptr_ IAdviseSink **ppAdvSink);

        END_INTERFACE
    } IViewObjectVtbl;

    interface IViewObject
    {
        CONST_VTBL struct IViewObjectVtbl *lpVtbl;
    };

#ifdef COBJMACROS

#define IViewObject_QueryInterface(This, riid, ppvObject) ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IViewObject_AddRef(This) ((This)->lpVtbl->AddRef(This))

#define IViewObject_Release(This) ((This)->lpVtbl->Release(This))

#define IViewObject_Draw(This, dwDrawAspect, lindex, pvAspect, ptd, hdcTargetDev, hdcDraw, lprcBounds, lprcWBounds, pfnContinue, dwContinue) ((This)->lpVtbl->Draw(This, dwDrawAspect, lindex, pvAspect, ptd, hdcTargetDev, hdcDraw, lprcBounds, lprcWBounds, pfnContinue, dwContinue))

#define IViewObject_GetColorSet(This, dwDrawAspect, lindex, pvAspect, ptd, hicTargetDev, ppColorSet) ((This)->lpVtbl->GetColorSet(This, dwDrawAspect, lindex, pvAspect, ptd, hicTargetDev, ppColorSet))

#define IViewObject_Freeze(This, dwDrawAspect, lindex, pvAspect, pdwFreeze) ((This)->lpVtbl->Freeze(This, dwDrawAspect, lindex, pvAspect, pdwFreeze))

#define IViewObject_Unfreeze(This, dwFreeze) ((This)->lpVtbl->Unfreeze(This, dwFreeze))

#define IViewObject_SetAdvise(This, aspects, advf, pAdvSink) ((This)->lpVtbl->SetAdvise(This, aspects, advf, pAdvSink))

#define IViewObject_GetAdvise(This, pAspects, pAdvf, ppAdvSink) ((This)->lpVtbl->GetAdvise(This, pAspects, pAdvf, ppAdvSink))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IViewObject_INTERFACE_DEFINED__ */

#ifndef __IViewObject2_INTERFACE_DEFINED__
#define __IViewObject2_INTERFACE_DEFINED__

    /* interface IViewObject2 */
    /* [uuid][object] */

    typedef /* [unique] */ __RPC_unique_pointer IViewObject2 *LPVIEWOBJECT2;

#if defined(__cplusplus) && !defined(CINTERFACE)

    DEFINE_GUID(IID_IViewObject2, 0x00000127, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
    struct IViewObject2 : public IViewObject
    {
      public:
        virtual HRESULT STDMETHODCALLTYPE GetExtent(
            /* [in] */ DWORD dwDrawAspect,
            /* [in] */ LONG lindex,
            /* [unique][in] */ __RPC__in_opt DVTARGETDEVICE *ptd,
            /* [out] */ __RPC__out LPSIZEL lpsizel)
            = 0;
    };

#else /* C style interface */

    typedef struct IViewObject2Vtbl
    {
        BEGIN_INTERFACE

        HRESULT(STDMETHODCALLTYPE *QueryInterface)
        (__RPC__in IViewObject2 *This,
         /* [in] */ __RPC__in REFIID riid,
         /* [annotation][iid_is][out] */
         _COM_Outptr_ void **ppvObject);

        ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IViewObject2 *This);

        ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IViewObject2 *This);

        /* [local] */ HRESULT(STDMETHODCALLTYPE *Draw)(IViewObject2 *This,
                                                       /* [annotation][in] */
                                                       _In_ DWORD dwDrawAspect,
                                                       /* [annotation][in] */
                                                       _In_ LONG lindex,
                                                       /* [annotation][unique][in] */
                                                       _Null_ void *pvAspect,
                                                       /* [annotation][unique][in] */
                                                       _In_opt_ DVTARGETDEVICE *ptd,
                                                       /* [annotation][in] */
                                                       _In_opt_ HDC hdcTargetDev,
                                                       /* [annotation][in] */
                                                       _In_ HDC hdcDraw,
                                                       /* [annotation][in] */
                                                       _In_opt_ LPCRECTL lprcBounds,
                                                       /* [annotation][unique][in] */
                                                       _In_opt_ LPCRECTL lprcWBounds,
                                                       /* [annotation][in] */
                                                       _In_opt_ BOOL(STDMETHODCALLTYPE *pfnContinue)(ULONG_PTR dwContinue),
                                                       /* [annotation][in] */
                                                       _In_ ULONG_PTR dwContinue);

        /* [local] */ HRESULT(STDMETHODCALLTYPE *GetColorSet)(IViewObject2 *This,
                                                              /* [annotation][in] */
                                                              _In_ DWORD dwDrawAspect,
                                                              /* [annotation][in] */
                                                              _In_ LONG lindex,
                                                              /* [annotation][unique][in] */
                                                              _Null_ void *pvAspect,
                                                              /* [annotation][unique][in] */
                                                              _In_opt_ DVTARGETDEVICE *ptd,
                                                              /* [annotation][in] */
                                                              _In_opt_ HDC hicTargetDev,
                                                              /* [annotation][out] */
                                                              _Outptr_ LOGPALETTE **ppColorSet);

        /* [local] */ HRESULT(STDMETHODCALLTYPE *Freeze)(IViewObject2 *This,
                                                         /* [annotation][in] */
                                                         _In_ DWORD dwDrawAspect,
                                                         /* [annotation][in] */
                                                         _In_ LONG lindex,
                                                         /* [annotation][unique][in] */
                                                         _Null_ void *pvAspect,
                                                         /* [annotation][out] */
                                                         _Out_ DWORD *pdwFreeze);

        HRESULT(STDMETHODCALLTYPE *Unfreeze)
        (__RPC__in IViewObject2 *This,
         /* [in] */ DWORD dwFreeze);

        HRESULT(STDMETHODCALLTYPE *SetAdvise)
        (__RPC__in IViewObject2 *This,
         /* [in] */ DWORD aspects,
         /* [in] */ DWORD advf,
         /* [unique][in] */ __RPC__in_opt IAdviseSink *pAdvSink);

        /* [local] */ HRESULT(STDMETHODCALLTYPE *GetAdvise)(IViewObject2 *This,
                                                            /* [annotation][unique][out] */
                                                            _Out_opt_ DWORD *pAspects,
                                                            /* [annotation][unique][out] */
                                                            _Out_opt_ DWORD *pAdvf,
                                                            /* [annotation][out] */
                                                            _Outptr_ IAdviseSink **ppAdvSink);

        HRESULT(STDMETHODCALLTYPE *GetExtent)
        (__RPC__in IViewObject2 *This,
         /* [in] */ DWORD dwDrawAspect,
         /* [in] */ LONG lindex,
         /* [unique][in] */ __RPC__in_opt DVTARGETDEVICE *ptd,
         /* [out] */ __RPC__out LPSIZEL lpsizel);

        END_INTERFACE
    } IViewObject2Vtbl;

    interface IViewObject2
    {
        CONST_VTBL struct IViewObject2Vtbl *lpVtbl;
    };

#ifdef COBJMACROS

#define IViewObject2_QueryInterface(This, riid, ppvObject) ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IViewObject2_AddRef(This) ((This)->lpVtbl->AddRef(This))

#define IViewObject2_Release(This) ((This)->lpVtbl->Release(This))

#define IViewObject2_Draw(This, dwDrawAspect, lindex, pvAspect, ptd, hdcTargetDev, hdcDraw, lprcBounds, lprcWBounds, pfnContinue, dwContinue) ((This)->lpVtbl->Draw(This, dwDrawAspect, lindex, pvAspect, ptd, hdcTargetDev, hdcDraw, lprcBounds, lprcWBounds, pfnContinue, dwContinue))

#define IViewObject2_GetColorSet(This, dwDrawAspect, lindex, pvAspect, ptd, hicTargetDev, ppColorSet) ((This)->lpVtbl->GetColorSet(This, dwDrawAspect, lindex, pvAspect, ptd, hicTargetDev, ppColorSet))

#define IViewObject2_Freeze(This, dwDrawAspect, lindex, pvAspect, pdwFreeze) ((This)->lpVtbl->Freeze(This, dwDrawAspect, lindex, pvAspect, pdwFreeze))

#define IViewObject2_Unfreeze(This, dwFreeze) ((This)->lpVtbl->Unfreeze(This, dwFreeze))

#define IViewObject2_SetAdvise(This, aspects, advf, pAdvSink) ((This)->lpVtbl->SetAdvise(This, aspects, advf, pAdvSink))

#define IViewObject2_GetAdvise(This, pAspects, pAdvf, ppAdvSink) ((This)->lpVtbl->GetAdvise(This, pAspects, pAdvf, ppAdvSink))

#define IViewObject2_GetExtent(This, dwDrawAspect, lindex, ptd, lpsizel) ((This)->lpVtbl->GetExtent(This, dwDrawAspect, lindex, ptd, lpsizel))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IViewObject2_INTERFACE_DEFINED__ */

#ifndef __IDropSource_INTERFACE_DEFINED__
#define __IDropSource_INTERFACE_DEFINED__

    /* interface IDropSource */
    /* [uuid][object][local] */

    typedef /* [unique] */ IDropSource *LPDROPSOURCE;

#if defined(__cplusplus) && !defined(CINTERFACE)

    DEFINE_GUID(IID_IDropSource, 0x00000121, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
    struct IDropSource : public IUnknown
    {
      public:
        virtual HRESULT STDMETHODCALLTYPE QueryContinueDrag(
            /* [annotation][in] */
            _In_ BOOL fEscapePressed,
            /* [annotation][in] */
            _In_ DWORD grfKeyState)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE GiveFeedback(
            /* [annotation][in] */
            _In_ DWORD dwEffect)
            = 0;
    };

#else /* C style interface */

    typedef struct IDropSourceVtbl
    {
        BEGIN_INTERFACE

        HRESULT(STDMETHODCALLTYPE *QueryInterface)
        (IDropSource *This,
         /* [in] */ REFIID riid,
         /* [annotation][iid_is][out] */
         _COM_Outptr_ void **ppvObject);

        ULONG(STDMETHODCALLTYPE *AddRef)(IDropSource *This);

        ULONG(STDMETHODCALLTYPE *Release)(IDropSource *This);

        HRESULT(STDMETHODCALLTYPE *QueryContinueDrag)
        (IDropSource *This,
         /* [annotation][in] */
         _In_ BOOL fEscapePressed,
         /* [annotation][in] */
         _In_ DWORD grfKeyState);

        HRESULT(STDMETHODCALLTYPE *GiveFeedback)
        (IDropSource *This,
         /* [annotation][in] */
         _In_ DWORD dwEffect);

        END_INTERFACE
    } IDropSourceVtbl;

    interface IDropSource
    {
        CONST_VTBL struct IDropSourceVtbl *lpVtbl;
    };

#ifdef COBJMACROS

#define IDropSource_QueryInterface(This, riid, ppvObject) ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IDropSource_AddRef(This) ((This)->lpVtbl->AddRef(This))

#define IDropSource_Release(This) ((This)->lpVtbl->Release(This))

#define IDropSource_QueryContinueDrag(This, fEscapePressed, grfKeyState) ((This)->lpVtbl->QueryContinueDrag(This, fEscapePressed, grfKeyState))

#define IDropSource_GiveFeedback(This, dwEffect) ((This)->lpVtbl->GiveFeedback(This, dwEffect))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IDropSource_INTERFACE_DEFINED__ */

#ifndef __IDropTarget_INTERFACE_DEFINED__
#define __IDropTarget_INTERFACE_DEFINED__

    /* interface IDropTarget */
    /* [unique][uuid][object] */

    typedef /* [unique] */ __RPC_unique_pointer IDropTarget *LPDROPTARGET;

#define DROPEFFECT_NONE (0)

#define DROPEFFECT_COPY (1)

#define DROPEFFECT_MOVE (2)

#define DROPEFFECT_LINK (4)

#define DROPEFFECT_SCROLL (0x80000000)

// default inset-width of the hot zone, in pixels
//   typical use: GetProfileInt("windows","DragScrollInset",DD_DEFSCROLLINSET)
#define DD_DEFSCROLLINSET (11)

// default delay before scrolling, in milliseconds
//   typical use: GetProfileInt("windows","DragScrollDelay",DD_DEFSCROLLDELAY)
#define DD_DEFSCROLLDELAY (50)

// default scroll interval, in milliseconds
//   typical use: GetProfileInt("windows","DragScrollInterval", DD_DEFSCROLLINTERVAL)
#define DD_DEFSCROLLINTERVAL (50)

// default delay before dragging should start, in milliseconds
//   typical use: GetProfileInt("windows", "DragDelay", DD_DEFDRAGDELAY)
#define DD_DEFDRAGDELAY (200)

// default minimum distance (radius) before dragging should start, in pixels
//   typical use: GetProfileInt("windows", "DragMinDist", DD_DEFDRAGMINDIST)
#define DD_DEFDRAGMINDIST (2)

#if defined(__cplusplus) && !defined(CINTERFACE)

    DEFINE_GUID(IID_IDropTarget, 0x00000122, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
    struct IDropTarget : public IUnknown
    {
      public:
        virtual HRESULT STDMETHODCALLTYPE DragEnter(
            /* [unique][in] */ __RPC__in_opt IDataObject *pDataObj,
            /* [in] */ DWORD grfKeyState,
            /* [in] */ POINTL pt,
            /* [out][in] */ __RPC__inout DWORD *pdwEffect)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE DragOver(
            /* [in] */ DWORD grfKeyState,
            /* [in] */ POINTL pt,
            /* [out][in] */ __RPC__inout DWORD *pdwEffect)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE DragLeave(void) = 0;

        virtual HRESULT STDMETHODCALLTYPE Drop(
            /* [unique][in] */ __RPC__in_opt IDataObject *pDataObj,
            /* [in] */ DWORD grfKeyState,
            /* [in] */ POINTL pt,
            /* [out][in] */ __RPC__inout DWORD *pdwEffect)
            = 0;
    };

#else /* C style interface */

    typedef struct IDropTargetVtbl
    {
        BEGIN_INTERFACE

        HRESULT(STDMETHODCALLTYPE *QueryInterface)
        (__RPC__in IDropTarget *This,
         /* [in] */ __RPC__in REFIID riid,
         /* [annotation][iid_is][out] */
         _COM_Outptr_ void **ppvObject);

        ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IDropTarget *This);

        ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IDropTarget *This);

        HRESULT(STDMETHODCALLTYPE *DragEnter)
        (__RPC__in IDropTarget *This,
         /* [unique][in] */ __RPC__in_opt IDataObject *pDataObj,
         /* [in] */ DWORD grfKeyState,
         /* [in] */ POINTL pt,
         /* [out][in] */ __RPC__inout DWORD *pdwEffect);

        HRESULT(STDMETHODCALLTYPE *DragOver)
        (__RPC__in IDropTarget *This,
         /* [in] */ DWORD grfKeyState,
         /* [in] */ POINTL pt,
         /* [out][in] */ __RPC__inout DWORD *pdwEffect);

        HRESULT(STDMETHODCALLTYPE *DragLeave)(__RPC__in IDropTarget *This);

        HRESULT(STDMETHODCALLTYPE *Drop)
        (__RPC__in IDropTarget *This,
         /* [unique][in] */ __RPC__in_opt IDataObject *pDataObj,
         /* [in] */ DWORD grfKeyState,
         /* [in] */ POINTL pt,
         /* [out][in] */ __RPC__inout DWORD *pdwEffect);

        END_INTERFACE
    } IDropTargetVtbl;

    interface IDropTarget
    {
        CONST_VTBL struct IDropTargetVtbl *lpVtbl;
    };

#ifdef COBJMACROS

#define IDropTarget_QueryInterface(This, riid, ppvObject) ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IDropTarget_AddRef(This) ((This)->lpVtbl->AddRef(This))

#define IDropTarget_Release(This) ((This)->lpVtbl->Release(This))

#define IDropTarget_DragEnter(This, pDataObj, grfKeyState, pt, pdwEffect) ((This)->lpVtbl->DragEnter(This, pDataObj, grfKeyState, pt, pdwEffect))

#define IDropTarget_DragOver(This, grfKeyState, pt, pdwEffect) ((This)->lpVtbl->DragOver(This, grfKeyState, pt, pdwEffect))

#define IDropTarget_DragLeave(This) ((This)->lpVtbl->DragLeave(This))

#define IDropTarget_Drop(This, pDataObj, grfKeyState, pt, pdwEffect) ((This)->lpVtbl->Drop(This, pDataObj, grfKeyState, pt, pdwEffect))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IDropTarget_INTERFACE_DEFINED__ */

#ifndef __IDropSourceNotify_INTERFACE_DEFINED__
#define __IDropSourceNotify_INTERFACE_DEFINED__

    /* interface IDropSourceNotify */
    /* [unique][uuid][object][local] */

    EXTERN_C const IID IID_IDropSourceNotify;

#if defined(__cplusplus) && !defined(CINTERFACE)

    DEFINE_GUID(IID_IDropSourceNotify, 0x0000012B, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
    struct IDropSourceNotify : public IUnknown
    {
      public:
        virtual HRESULT STDMETHODCALLTYPE DragEnterTarget(
            /* [annotation][in] */
            _In_ HWND hwndTarget)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE DragLeaveTarget(void) = 0;
    };

#else /* C style interface */

    typedef struct IDropSourceNotifyVtbl
    {
        BEGIN_INTERFACE

        HRESULT(STDMETHODCALLTYPE *QueryInterface)
        (IDropSourceNotify *This,
         /* [in] */ REFIID riid,
         /* [annotation][iid_is][out] */
         _COM_Outptr_ void **ppvObject);

        ULONG(STDMETHODCALLTYPE *AddRef)(IDropSourceNotify *This);

        ULONG(STDMETHODCALLTYPE *Release)(IDropSourceNotify *This);

        HRESULT(STDMETHODCALLTYPE *DragEnterTarget)
        (IDropSourceNotify *This,
         /* [annotation][in] */
         _In_ HWND hwndTarget);

        HRESULT(STDMETHODCALLTYPE *DragLeaveTarget)(IDropSourceNotify *This);

        END_INTERFACE
    } IDropSourceNotifyVtbl;

    interface IDropSourceNotify
    {
        CONST_VTBL struct IDropSourceNotifyVtbl *lpVtbl;
    };

#ifdef COBJMACROS

#define IDropSourceNotify_QueryInterface(This, riid, ppvObject) ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IDropSourceNotify_AddRef(This) ((This)->lpVtbl->AddRef(This))

#define IDropSourceNotify_Release(This) ((This)->lpVtbl->Release(This))

#define IDropSourceNotify_DragEnterTarget(This, hwndTarget) ((This)->lpVtbl->DragEnterTarget(This, hwndTarget))

#define IDropSourceNotify_DragLeaveTarget(This) ((This)->lpVtbl->DragLeaveTarget(This))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IDropSourceNotify_INTERFACE_DEFINED__ */

#ifndef __IEnterpriseDropTarget_INTERFACE_DEFINED__
#define __IEnterpriseDropTarget_INTERFACE_DEFINED__

    /* interface IEnterpriseDropTarget */
    /* [unique][uuid][object] */

#if defined(__cplusplus) && !defined(CINTERFACE)

    DEFINE_GUID(IID_IEnterpriseDropTarget, 0x390E3878, 0xFD55, 0x4E18, 0x81, 0x9D, 0x46, 0x82, 0x08, 0x1C, 0x0C, 0xFD);
    struct IEnterpriseDropTarget : public IUnknown
    {
      public:
        virtual HRESULT STDMETHODCALLTYPE SetDropSourceEnterpriseId(
            /* [in] */ __RPC__in LPCWSTR identity)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE IsEvaluatingEdpPolicy(
            /* [retval][out] */ __RPC__out BOOL *value)
            = 0;
    };

#else /* C style interface */

    typedef struct IEnterpriseDropTargetVtbl
    {
        BEGIN_INTERFACE

        HRESULT(STDMETHODCALLTYPE *QueryInterface)
        (__RPC__in IEnterpriseDropTarget *This,
         /* [in] */ __RPC__in REFIID riid,
         /* [annotation][iid_is][out] */
         _COM_Outptr_ void **ppvObject);

        ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IEnterpriseDropTarget *This);

        ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IEnterpriseDropTarget *This);

        HRESULT(STDMETHODCALLTYPE *SetDropSourceEnterpriseId)
        (__RPC__in IEnterpriseDropTarget *This,
         /* [in] */ __RPC__in LPCWSTR identity);

        HRESULT(STDMETHODCALLTYPE *IsEvaluatingEdpPolicy)
        (__RPC__in IEnterpriseDropTarget *This,
         /* [retval][out] */ __RPC__out BOOL *value);

        END_INTERFACE
    } IEnterpriseDropTargetVtbl;

    interface IEnterpriseDropTarget
    {
        CONST_VTBL struct IEnterpriseDropTargetVtbl *lpVtbl;
    };

#ifdef COBJMACROS

#define IEnterpriseDropTarget_QueryInterface(This, riid, ppvObject) ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IEnterpriseDropTarget_AddRef(This) ((This)->lpVtbl->AddRef(This))

#define IEnterpriseDropTarget_Release(This) ((This)->lpVtbl->Release(This))

#define IEnterpriseDropTarget_SetDropSourceEnterpriseId(This, identity) ((This)->lpVtbl->SetDropSourceEnterpriseId(This, identity))

#define IEnterpriseDropTarget_IsEvaluatingEdpPolicy(This, value) ((This)->lpVtbl->IsEvaluatingEdpPolicy(This, value))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IEnterpriseDropTarget_INTERFACE_DEFINED__ */

    /* interface __MIDL_itf_oleidl_0000_0024 */
    /* [local] */

#define CFSTR_ENTERPRISE_ID (TEXT("EnterpriseDataProtectionId"))

#ifndef __IEnumOLEVERB_INTERFACE_DEFINED__
#define __IEnumOLEVERB_INTERFACE_DEFINED__

    /* interface IEnumOLEVERB */
    /* [unique][uuid][object] */

    typedef /* [unique] */ __RPC_unique_pointer IEnumOLEVERB *LPENUMOLEVERB;

    typedef struct tagOLEVERB
    {
        LONG lVerb;
        LPOLESTR lpszVerbName;
        DWORD fuFlags;
        DWORD grfAttribs;
    } OLEVERB;

    typedef struct tagOLEVERB *LPOLEVERB;

    typedef /* [v1_enum] */
        enum tagOLEVERBATTRIB
    {
        OLEVERBATTRIB_NEVERDIRTIES = 1,
        OLEVERBATTRIB_ONCONTAINERMENU = 2
    } OLEVERBATTRIB;

    EXTERN_C const IID IID_IEnumOLEVERB;

#if defined(__cplusplus) && !defined(CINTERFACE)

    DEFINE_GUID(IID_IEnumOLEVERB, 0x00000104, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
    struct IEnumOLEVERB : public IUnknown
    {
      public:
        virtual /* [local] */ HRESULT STDMETHODCALLTYPE Next(
            /* [annotation][in] */
            _In_ ULONG celt,
            /* [annotation][length_is][size_is][out] */
            _Out_writes_to_(celt, *pceltFetched) LPOLEVERB rgelt,
            /* [annotation][out] */
            _Out_opt_ ULONG *pceltFetched)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE Skip(
            /* [in] */ ULONG celt)
            = 0;

        virtual HRESULT STDMETHODCALLTYPE Reset(void) = 0;

        virtual HRESULT STDMETHODCALLTYPE Clone(
            /* [out] */ __RPC__deref_out_opt IEnumOLEVERB **ppenum)
            = 0;
    };

#else /* C style interface */

    typedef struct IEnumOLEVERBVtbl
    {
        BEGIN_INTERFACE

        HRESULT(STDMETHODCALLTYPE *QueryInterface)
        (__RPC__in IEnumOLEVERB *This,
         /* [in] */ __RPC__in REFIID riid,
         /* [annotation][iid_is][out] */
         _COM_Outptr_ void **ppvObject);

        ULONG(STDMETHODCALLTYPE *AddRef)(__RPC__in IEnumOLEVERB *This);

        ULONG(STDMETHODCALLTYPE *Release)(__RPC__in IEnumOLEVERB *This);

        /* [local] */ HRESULT(STDMETHODCALLTYPE *Next)(IEnumOLEVERB *This,
                                                       /* [annotation][in] */
                                                       _In_ ULONG celt,
                                                       /* [annotation][length_is][size_is][out] */
                                                       _Out_writes_to_(celt, *pceltFetched) LPOLEVERB rgelt,
                                                       /* [annotation][out] */
                                                       _Out_opt_ ULONG *pceltFetched);

        HRESULT(STDMETHODCALLTYPE *Skip)
        (__RPC__in IEnumOLEVERB *This,
         /* [in] */ ULONG celt);

        HRESULT(STDMETHODCALLTYPE *Reset)(__RPC__in IEnumOLEVERB *This);

        HRESULT(STDMETHODCALLTYPE *Clone)
        (__RPC__in IEnumOLEVERB *This,
         /* [out] */ __RPC__deref_out_opt IEnumOLEVERB **ppenum);

        END_INTERFACE
    } IEnumOLEVERBVtbl;

    interface IEnumOLEVERB
    {
        CONST_VTBL struct IEnumOLEVERBVtbl *lpVtbl;
    };

#ifdef COBJMACROS

#define IEnumOLEVERB_QueryInterface(This, riid, ppvObject) ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IEnumOLEVERB_AddRef(This) ((This)->lpVtbl->AddRef(This))

#define IEnumOLEVERB_Release(This) ((This)->lpVtbl->Release(This))

#define IEnumOLEVERB_Next(This, celt, rgelt, pceltFetched) ((This)->lpVtbl->Next(This, celt, rgelt, pceltFetched))

#define IEnumOLEVERB_Skip(This, celt) ((This)->lpVtbl->Skip(This, celt))

#define IEnumOLEVERB_Reset(This) ((This)->lpVtbl->Reset(This))

#define IEnumOLEVERB_Clone(This, ppenum) ((This)->lpVtbl->Clone(This, ppenum))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IEnumOLEVERB_INTERFACE_DEFINED__ */

#ifdef __cplusplus
}
#endif

#endif
