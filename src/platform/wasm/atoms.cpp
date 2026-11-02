#include "atoms.h"
#include "lowmem.h"

SAtoms::SAtoms()
    : m_nextAtom(0xC000)
{
}

SAtoms &SAtoms::instance()
{
    static SAtoms inst;
    return inst;
}

ATOM SAtoms::registerAtom(LPCSTR name){
    return SAtoms::instance()._registerAtom(name);
}
ATOM SAtoms::_registerAtom(LPCSTR name)
{
    /* 第一次把窗口类名（字符串常量）登记成原子。此时数据段布局已经决定了
     * IS_INTRESOURCE 还能不能用，见 lowmem.h：不满足就当场终止，别等到
     * CreateWindowExA 拿一个查不到的原子号返回 0。 */
    swinx_wasmCheckLowMemoryFree();
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_atomsA.find(name);
    if (it != m_atomsA.end())
        return it->second;
    ATOM atom = m_nextAtom++;
    m_atomsA[name] = atom;
    m_atomNamesA[atom] = name;
    return atom;
}

UINT SAtoms::getAtomName(ATOM atom, LPSTR buf, int bufLen){
    return SAtoms::instance()._getAtomName(atom, buf, bufLen);
}
UINT SAtoms::_getAtomName(ATOM atom, LPSTR buf, int bufLen)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_atomNamesA.find(atom);
    if (it == m_atomNamesA.end())
        return 0;
    if(!buf)
        return it->second.length();
    int len = std::min<int>((int)it->second.length(), bufLen - 1);
    memcpy(buf, it->second.c_str(), len);
    buf[len] = 0;
    return len;
}