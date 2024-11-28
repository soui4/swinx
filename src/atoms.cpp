#include <windows.h>
#include <assert.h>
#include "atoms.h"
#include "SConnection.h"

xcb_atom_t SAtoms::internAtom(xcb_connection_t *connection, uint8_t onlyIfExist, const char *atomName)
{
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(connection, onlyIfExist, strlen(atomName), atomName);
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(connection, cookie, NULL);
    xcb_atom_t atom = XCB_NONE;
    if (reply)
    {
        atom = reply->atom;
        ::free(reply);
    }
    return atom;
}

void SAtoms::Init(xcb_connection_t *conn)
{
    const int kAtomCount = sizeof(SAtoms) / sizeof(xcb_atom_t);
    xcb_intern_atom_cookie_t cookies[kAtomCount];
    int kAtomCount2 = 0;
    const char **kAtomNames = AtomNames(kAtomCount2);
    assert(kAtomCount == kAtomCount2);
    for (int i = 0; i < kAtomCount; i++)
    {
        cookies[i] = xcb_intern_atom(conn, false, strlen(kAtomNames[i]), kAtomNames[i]);
    }

    xcb_atom_t* atom = (xcb_atom_t*)this;
    for (int i = 0; i < kAtomCount; i++) {
        xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(conn, cookies[i], 0);
        *atom = reply->atom;
        free(reply);
        atom++;
    }
}