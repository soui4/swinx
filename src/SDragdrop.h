#ifndef _SDRAGDROP_H_
#define _SDRAGDROP_H_
#include "nativewnd.h"
#include "SUnkImpl.h"
#include <vector>
#include "uimsg.h"

class SConnection;
class SDataObjectProxy : public SUnkImpl<IDataObject>{
    void initTypeList(const uint32_t data32[5]);
public:
    SDataObjectProxy(SConnection* conn, HWND hWnd,const uint32_t data32[5]);

    HWND getSource() const {
        return m_hSource;
    }

public:
    virtual /* [local] */ HRESULT STDMETHODCALLTYPE GetData(
        /* [annotation][unique][in] */
        _In_  FORMATETC* pformatetcIn,
        /* [annotation][out] */
        _Out_  STGMEDIUM* pmedium);

    virtual /* [local] */ HRESULT STDMETHODCALLTYPE GetDataHere(
        /* [annotation][unique][in] */
        _In_  FORMATETC* pformatetc,
        /* [annotation][out][in] */
        _Inout_  STGMEDIUM* pmedium) {
        return E_NOTIMPL;
    }

    virtual HRESULT STDMETHODCALLTYPE QueryGetData(
        /* [unique][in] */ __RPC__in_opt FORMATETC* pformatetc) {
        for (auto it : m_lstTypes) {
            if (it == pformatetc->cfFormat)
            {
                if (pformatetc->tymed != TYMED_HGLOBAL)
                    return DV_E_TYMED;
                else
                    return S_OK;
            }
        }
        return DV_E_FORMATETC;
    }

    virtual HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(
        /* [unique][in] */ __RPC__in_opt FORMATETC* pformatectIn,
        /* [out] */ __RPC__out FORMATETC* pformatetcOut) {
        return E_NOTIMPL;
    }

    virtual /* [local] */ HRESULT STDMETHODCALLTYPE SetData(
        /* [annotation][unique][in] */
        _In_  FORMATETC* pformatetc,
        /* [annotation][unique][in] */
        _In_  STGMEDIUM* pmedium,
        /* [in] */ BOOL fRelease) {
        return E_NOTIMPL;
    }

    virtual HRESULT STDMETHODCALLTYPE EnumFormatEtc(
        /* [in] */ DWORD dwDirection,
        /* [out] */ __RPC__deref_out_opt IEnumFORMATETC** ppenumFormatEtc) {
        return E_NOTIMPL;
    }

    virtual HRESULT STDMETHODCALLTYPE DAdvise(
        /* [in] */ __RPC__in FORMATETC* pformatetc,
        /* [in] */ DWORD advf,
        /* [unique][in] */ __RPC__in_opt IAdviseSink* pAdvSink,
        /* [out] */ __RPC__out DWORD* pdwConnection) {
        return E_NOTIMPL;
    }

    virtual HRESULT STDMETHODCALLTYPE DUnadvise(
        /* [in] */ DWORD dwConnection) {
        return E_NOTIMPL;
    }

    virtual HRESULT STDMETHODCALLTYPE EnumDAdvise(
        /* [out] */ __RPC__deref_out_opt IEnumSTATDATA** ppenumAdvise) {
        return E_NOTIMPL;
    }
public:
    IUNKNOWN_BEGIN(IDataObject)
        IUNKNOWN_END()
public:
    SConnection* m_conn;
    HWND m_hSource;
    std::vector<uint32_t> m_lstTypes;
    xcb_timestamp_t m_targetTime;
    DWORD m_dwEffect;
    DWORD m_dwKeyState;
    POINTL m_ptOver;
};

class SDragDrop : public CNativeWnd {
public:
    enum {
        xdnd_version = 5,
        xdnd_max_type = 100,
    };
    static HRESULT DoDragDrop(
        IDataObject* pDataObject,  /* [in] ptr to the data obj           */
        IDropSource* pDropSource,  /* [in] ptr to the source obj         */
        DWORD       dwOKEffect,    /* [in] effects allowed by the source */
        DWORD* pdwEffect);    /* [out] ptr to effects of the source */

public:
	SDragDrop(BOOL bAutoFree=FALSE):CNativeWnd(bAutoFree){
        curTargetHWND = 0;
        escPressed = FALSE;
        returnValue = E_ABORT;
    }
	~SDragDrop() {}

private:
    LRESULT TrackStateChange(UINT uMsg,WPARAM wp,LPARAM lp);
    LRESULT OnXdndStatus(UINT uMsg, WPARAM wp, LPARAM lp);
    LRESULT OnXdndFinish(UINT uMsg, WPARAM wp, LPARAM lp);

    void drag_leave(HWND target);

    void drag_over(HWND target, int x, int y);

    void drag_enter(HWND new_target);
    void drag_drop(HWND target,DWORD dwEffect);
    void drag_end();
    HRESULT give_feedback();
    BEGIN_MSG_MAP_EX(SDragDrop)
        MESSAGE_HANDLER_EX(WM_TIMER, TrackStateChange)
        MESSAGE_HANDLER_EX(WM_MOUSEMOVE, TrackStateChange)
        MESSAGE_HANDLER_EX(WM_LBUTTONUP, TrackStateChange)
        MESSAGE_HANDLER_EX(WM_MBUTTONUP, TrackStateChange)
        MESSAGE_HANDLER_EX(WM_RBUTTONUP, TrackStateChange)
        MESSAGE_HANDLER_EX(WM_LBUTTONDOWN, TrackStateChange)
        MESSAGE_HANDLER_EX(WM_MBUTTONDOWN, TrackStateChange)
        MESSAGE_HANDLER_EX(WM_RBUTTONDOWN, TrackStateChange)
        MESSAGE_HANDLER_EX(UM_XDND_STATUS, OnXdndStatus)
        MESSAGE_HANDLER_EX(UM_XDND_FINISH, OnXdndFinish)
        END_MSG_MAP()
private:
    IDataObject* dataObject;
    IDropSource* dropSource;
    DWORD        dwOKEffect;
    DWORD* pdwEffect;
    BOOL       trackingDone;
    BOOL         inTrackCall;
    HRESULT      returnValue;
    BOOL         dragEnded;
    BOOL       escPressed;
    HWND       curTargetHWND;	/* window the mouse is hovering over */
    POINTL     curMousePos;       /* current position of the mouse in screen coordinates */
    DWORD      dwKeyState;        /* current state of the shift and ctrl keys and the mouse buttons */
    SConnection* conn;
};

#endif//_SDRAGDROP_H_
