#ifndef _OHOS_ATOMS_H_
#define _OHOS_ATOMS_H_

#include <ctypes.h>

class SAtoms {
  public:
    static ATOM registerAtom(const char *name);
    static int getAtomName(ATOM atom, char *buf, int bufSize);
};

#endif // _OHOS_ATOMS_H_
