#ifndef __SUNKNOWNIMPL__H__
#define __SUNKNOWNIMPL__H__

#define IUNKNOWN_BEGIN2(iid, cls)                                    \
    STDMETHODIMP_(ULONG) AddRef(void) override                       \
    {                                                                \
        return _AddRef();                                            \
    }                                                                \
    STDMETHODIMP_(ULONG) Release(void) override                      \
    {                                                                \
        return _Release();                                           \
    }                                                                \
    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObj) override \
    {                                                                \
        if (ppvObj == NULL)                                          \
            return E_INVALIDARG;                                     \
        *ppvObj = NULL;                                              \
        if (IsEqualIID(riid, iid) || IsEqualIID(riid, IID_IUnknown)) \
        {                                                            \
            *ppvObj = (cls *)this;                                   \
        }

#define IUNKNOWN_ADD_IID2(iid, cls) \
    if (IsEqualIID(riid, iid))      \
    {                               \
        *ppvObj = (cls *)this;      \
    }

/* 把 QueryInterface 的匹配结果指向另一个对象（聚合/委托）时用。
 * 注意：IUNKNOWN_END 会把 *ppvObj 原样 reinterpret 成 IUnknown* 再调 AddRef，
 * 因此要求被返回接口的 IUnknown 位于对象偏移 0，即单继承链
 * （Iface : ... : IUnknown）。引入多继承或非主基类时这条宏不再成立。 */
#define IUNKNOWN_ADD_FIELD_IID2(iid, cls, field) \
    if (IsEqualIID(riid, iid))      \
    {                               \
        *ppvObj = (cls *)field;      \
    }

/* 通过返回的接口指针调用 AddRef，这样委托给其它对象的 QI 也能加对引用计数
 * （前提见 IUNKNOWN_ADD_FIELD_IID2 的说明）。 */
#define IUNKNOWN_END()    \
    if (*ppvObj)          \
    {                     \
        ((IUnknown*)(*ppvObj))->AddRef();         \
        return S_OK;      \
    }                     \
    return E_NOINTERFACE; \
    }

#define IUNKNOWN_BEGIN(iface) IUNKNOWN_BEGIN2(__suidof(iface), iface)

#define IUNKNOWN_ADD_IID(iface) IUNKNOWN_ADD_IID2(__suidof(iface), iface)
#define IUNKNOWN_ADD_FIELD_IID(iface, field) IUNKNOWN_ADD_FIELD_IID2(__suidof(iface), iface,field)

template<class T>
class  SUnkImpl : public T{
  public:
      SUnkImpl()
        : m_cRef(1)
    {
    }

    virtual ~SUnkImpl()
    {
    }

    virtual void OnFinalRelease()
    {
        delete this;
    }

  protected:
    STDMETHODIMP_(ULONG) _AddRef(void)
    {
        return (ULONG)InterlockedIncrement(&m_cRef);
    }

    STDMETHODIMP_(ULONG) _Release(void)
    {
        LONG ret = InterlockedDecrement(&m_cRef);
        if (ret == 0)
        {
            OnFinalRelease();
        }
        return ret;
    }

    LONG GetRef() const
    {
        return m_cRef;
    }

  private:
    LONG m_cRef;
};
#endif // __SUNKNOWNIMPL__H__