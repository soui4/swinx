#include "atoms.h"
#include <assert.h>

static const char *kGlobalShareAtomName = "/share_atom_A95AB24431E7";

int SAtoms::registerAtom(const char *name)
{

    return instance()._registerAtom(name);
}
int SAtoms::getAtom(const char *name)
{
    return instance()._getAtom(name);
}

int SAtoms::getAtomName(int atom, char *buf, int bufSize)
{
    const char * name = instance()._getName(atom);
    if (name)
    {
        if(!buf)
            return strlen(name);
        if (bufSize > strlen(name))
        {
            strcpy(buf, name);
            return strlen(name)+1;
        }
        else
        {
            return -1;
        }
    }else{
        return -1;
    }
}

SAtoms &SAtoms::instance()
{
    static SAtoms inst(kGlobalShareAtomName);
    return inst;
}

const char *SAtoms::_getName(int atom)
{
    if (atom < 0 || atom >= kMaxAtomSize)
    {
        return NULL;
    }
    const char *ret = NULL;
    m_shm.getRwLock()->lockShared();
    BYTE *buf = m_shm.buffer();
    uint32_t &nRef = *(uint32_t *)buf;
    if (atom < nRef)
    {
        ret = (char *)(buf + sizeof(uint32_t) + atom * kMaxNameLength);
    }
    m_shm.getRwLock()->unlockShared();
    return ret;
}

int SAtoms::_getAtom(const char *name)
{
    if (name == NULL || strlen(name) >= kMaxNameLength)
    {
        return -1;
    }
    m_shm.getRwLock()->lockShared();
    BYTE *buf = m_shm.buffer();
    uint32_t &nRef = *(uint32_t *)buf;
    char *pName = (char *)(buf + sizeof(uint32_t));
    for (int i = 0; i < nRef && nRef < kMaxAtomSize; i++)
    {
        if (strncmp(pName, name, kMaxNameLength) == 0)
        {
            m_shm.getRwLock()->unlockShared();
            return i;
        }
        pName += kMaxNameLength;
    }
    m_shm.getRwLock()->unlockShared();
    return -1;
}

int SAtoms::_registerAtom(const char *name)
{
    int find = _getAtom(name);
    if (find != -1)
    {
        return find;
    }
    m_shm.getRwLock()->lockExclusive();
    BYTE *buf = m_shm.buffer();
    uint32_t &nRef = *(uint32_t *)buf;
    char *pName = (char *)(buf + sizeof(uint32_t) + nRef * kMaxNameLength);
    strncpy(pName, name, kMaxNameLength);
    int ret = nRef++;
    m_shm.getRwLock()->unlockExclusive();
    return ret;
}

SAtoms::SAtoms(const char *name)
{
    swinx::SharedMemory::InitStat ret = m_shm.init(name, kMaxNameLength * kMaxAtomSize + sizeof(uint32_t));
    assert(ret);
    {
        m_shm.getRwLock()->lockExclusive();
        if (ret == swinx::SharedMemory::Created)
        {
            *(uint32_t *)m_shm.buffer() = 0;
        }
        m_shm.getRwLock()->unlockExclusive();
    }
}
SAtoms::~SAtoms()
{
}