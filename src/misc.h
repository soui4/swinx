#ifndef _MISC_H_
#define _MISC_H_

#include <ctypes.h>
class CPoint : public POINT {
  public:
    CPoint(LONG _x, LONG _y)
    {
        x = _x;
        y = _y;
    }
};

#endif //_MISC_H_