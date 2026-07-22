#ifndef _ANDROID_ATOMS_H_
#define _ANDROID_ATOMS_H_

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
    std::map<std::string, ATOM> m_atomsA;
    std::map<ATOM, std::string> m_atomNamesA;
    std::recursive_mutex m_mutex;
    ATOM m_nextAtom;
};

#endif // _ANDROID_ATOMS_H_