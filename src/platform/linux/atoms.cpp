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

void SAtoms::Init(xcb_connection_t *conn, int nScrNo)
{
    const int kAtomCount = sizeof(SAtoms) / sizeof(xcb_atom_t);
    xcb_intern_atom_cookie_t cookies[kAtomCount];
    int kAtomCount2 = 0;
    const char **kAtomNames = AtomNames(kAtomCount2);
    assert(kAtomCount == kAtomCount2);
    for (int i = 0; i < kAtomCount; i++)
    {
        if (strchr(kAtomNames[i], '%') != nullptr)
        {
            char szAtomName[200];
            int len = _snprintf(szAtomName, 200, kAtomNames[i], nScrNo);
            cookies[i] = xcb_intern_atom(conn, false, len, szAtomName);
        }
        else
        {
            cookies[i] = xcb_intern_atom(conn, false, strlen(kAtomNames[i]), kAtomNames[i]);
        }
    }

    xcb_atom_t *atom = (xcb_atom_t *)this;
    for (int i = 0; i < kAtomCount; i++)
    {
        xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(conn, cookies[i], 0);
        *atom = reply->atom;
        free(reply);
        atom++;
    }
}

int SAtoms::getAtomName(xcb_atom_t atom,char *buf,int bufSize){
    SConnection * conn = SConnMgr::instance()->getConnection();
    xcb_get_atom_name_cookie_t cookie = xcb_get_atom_name(conn->connection, atom);
    xcb_get_atom_name_reply_t *reply = xcb_get_atom_name_reply(conn->connection, cookie, nullptr);
    if (reply)
    {
        char *atom_name = xcb_get_atom_name_name(reply);
        if (atom_name)
        {
            int len = xcb_get_atom_name_name_length(reply);
            if(buf){
                if(bufSize > len){
                    strncpy(buf, atom_name,len);
                    buf[len] = 0;
                    return len+1;
                }else{
                    return -1;
                }
            }else{
                return len;
            }
        }
        free(reply);
    }else if(atom > 10000){
        //user defined atom
        return snprintf(buf,bufSize,"#%d",atom);
    }
    return 0;
}

xcb_atom_t SAtoms::registerAtom(const char *name,xcb_connection_t *xcb_conn)
{
    if(xcb_conn){
        return internAtom(xcb_conn, 0, name);
    }else{
        SConnection *conn = SConnMgr::instance()->getConnection();
        return internAtom(conn->connection, 0, name);
    }
}
