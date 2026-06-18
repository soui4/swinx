#include "atoms.h"

#include <map>
#include <mutex>
#include <string>
#include <string.h>
#include <stdio.h>

namespace {

struct AtomTable {
    std::mutex mutex;
    ATOM nextAtom = 1;
    std::map<std::string, ATOM> nameToAtom;
    std::map<ATOM, std::string> atomToName;
};

AtomTable &table()
{
    static AtomTable inst;
    return inst;
}

} // namespace

ATOM SAtoms::registerAtom(const char *name)
{
    if (!name || !*name)
        return 0;

    AtomTable &atoms = table();
    std::lock_guard<std::mutex> lock(atoms.mutex);

    std::string key(name);
    auto it = atoms.nameToAtom.find(key);
    if (it != atoms.nameToAtom.end())
        return it->second;

    ATOM atom = atoms.nextAtom++;
    atoms.nameToAtom[key] = atom;
    atoms.atomToName[atom] = key;
    return atom;
}

int SAtoms::getAtomName(ATOM atom, char *buf, int bufSize)
{
    AtomTable &atoms = table();
    std::lock_guard<std::mutex> lock(atoms.mutex);

    auto it = atoms.atomToName.find(atom);
    if (it == atoms.atomToName.end())
    {
        if (atom > 0xffff)
            return buf ? snprintf(buf, bufSize, "#%u", atom) : 0;
        return 0;
    }

    const std::string &name = it->second;
    if (!buf)
        return static_cast<int>(name.length());
    if (bufSize <= static_cast<int>(name.length()))
        return -1;
    strcpy(buf, name.c_str());
    return static_cast<int>(name.length() + 1);
}
