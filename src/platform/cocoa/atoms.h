#ifndef __ATOMS_H__
#define __ATOMS_H__
#include <ctypes.h>
#include <map>
#include <string>
#include <sharedmem.h>

class SAtoms{
    enum{
         kMaxNameLength = 255,
         kMaxAtomSize = 10000,
    };

public:
    static int registerAtom( const char *name );
    static int getAtomName( int atom , char *buf , int bufSize );
private:
    static SAtoms & instance();

    const char *  _getName( int atom );

    int _getAtom( const char *name );

    int _registerAtom( const char *name );

private:
    SAtoms(const char *name);
    ~SAtoms();

    swinx::SharedMemory m_shm;
};


#endif//__ATOMS_H__