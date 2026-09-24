#ifndef _SWINX_FREERTOS_ATOMS_H_
#define _SWINX_FREERTOS_ATOMS_H_

#include <windows.h>
#include <map>
#include <string>
#include <mutex>

class SAtoms {
public:
    static ATOM registerAtom(LPCSTR name);
    static UINT getAtomName(ATOM atom, LPSTR buf, int bufLen);
private:
    ATOM _registerAtom(LPCSTR name);
    UINT _getAtomName(ATOM atom, LPSTR buf, int bufLen);

    static SAtoms &instance();
    SAtoms();
    swinx_stl::map<swinx_stl::string, ATOM> m_atomsA;
    swinx_stl::map<ATOM, swinx_stl::string> m_atomNamesA;
    std::recursive_mutex m_mutex;
    ATOM m_nextAtom;
};

#endif // _SWINX_FREERTOS_ATOMS_H_