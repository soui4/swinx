#include <windows.h>
#include <commdlg.h>
#include "tostring.hpp"
#include "gtk/gtkdlghelper.h"
#include "log.h"
#define kLogTag "commondlg"

static BOOL _GetOpenFileNameA(LPOPENFILENAMEA p,Gtk::Mode mode)
{
    #ifdef ENABLE_GTK
    std::list<std::string> lstRet;
    bool ret = Gtk::openGtkFileChooser(lstRet,p->hwndOwner,mode,"","",p->lpstrInitialDir?p->lpstrInitialDir:"",p->lpstrFilter?p->lpstrFilter:"",NULL,p->Flags&OFN_ALLOWMULTISELECT);
    if(!ret || lstRet.empty())
        return FALSE;
    auto it = lstRet.begin();
    std::string &file = *it;
    int pos = file.rfind('/');
    if(!pos) 
        return FALSE;
    if(p->lpstrFileTitle && p->nMaxFileTitle > file.length()-(pos+1)){
        strcpy(p->lpstrFileTitle,file.c_str()+pos+1);
    }
    p->nFileOffset = 0;
    if(p->Flags&OFN_ALLOWMULTISELECT){
        std::stringstream ss;
        std::string path=file.substr(0,pos);
        ss<<path;
        ss<<"\0";
        p->nFileOffset = pos+1;
        while(it != lstRet.end()){
            file = *it;
            if(file.length()>pos+1){
                ss<<file.substr(pos+1);
                ss<<"\0";
            }
            it++;
        }
        ss<<"\0";
        if(p->nMaxFile < ss.str().length()){
            SetLastError(SEC_E_INSUFFICIENT_MEMORY);
            return FALSE;
        }
        memcpy(p->lpstrFile,ss.str().c_str(),ss.str().length());
    }else{
        if(p->nMaxFile < lstRet.front().length())
        {
            SetLastError(SEC_E_INSUFFICIENT_MEMORY);
            return FALSE;
        }    
        strcpy(p->lpstrFile,lstRet.front().c_str());
    }
    return TRUE;
    #else
    return FALSE;
    #endif//ENABLE_GTK
}

BOOL _GetOpenFileNameW(LPOPENFILENAMEW p,Gtk::Mode mode)
{
    std::string strFilter,strCustomFilter,strFile,strFileTitle,strInitDir,strTitle,strDefExt;
    tostring(p->lpstrFilter,-1,strFilter);
    tostring(p->lpstrCustomFilter,-1,strCustomFilter);
    tostring(p->lpstrFileTitle,-1,strFileTitle);
    tostring(p->lpstrInitialDir,-1,strInitDir);
    tostring(p->lpstrTitle,-1,strTitle);
    tostring(p->lpstrDefExt,-1,strDefExt);
    OPENFILENAMEA openFile;
    openFile. lStructSize = sizeof(openFile);
    openFile. hwndOwner = p->hwndOwner;
    openFile. hInstance=p->hInstance;
    openFile. lpstrFilter = strFilter.c_str();
    openFile. nMaxCustFilter = p->nMaxCustFilter;
    openFile. nFilterIndex=p->nFilterIndex;
    openFile.lpstrFile = new char[p->nMaxFile];
    openFile.lpstrFile[0]=0;
    openFile. nMaxFile=p->nMaxFile;
    openFile.lpstrFileTitle = new char[p->nMaxFileTitle];
    openFile.lpstrFileTitle[0]=0;
    openFile. nMaxFileTitle=p->nMaxFileTitle;
    openFile. lpstrInitialDir=strInitDir.c_str();
    openFile. lpstrTitle=strTitle.c_str();
    openFile. Flags=p->Flags;
    openFile. nFileExtension=p->nFileExtension;
    openFile. lpstrDefExt = strDefExt.c_str();
    openFile. lCustData=p->lCustData;
    openFile. FlagsEx=p->FlagsEx;

    BOOL  ret = _GetOpenFileNameA(&openFile,mode);
    {
        std::wstring str;
        towstring(openFile.lpstrFileTitle,-1,str);
        delete []openFile.lpstrFileTitle;
        if(str.length()<p->nMaxFileTitle){
            wcscpy(p->lpstrFileTitle,str.c_str());
        }
    }
    {
        std::wstring str;
        towstring(openFile.lpstrFile,-1,str);
        delete []openFile.lpstrFile;
        if(str.length()<p->nMaxFile){
            wcscpy(p->lpstrFile,str.c_str());
        }else{
            ret = FALSE;
        }
        if(openFile.nFileOffset){
            p->nFileOffset = MultiByteToWideChar(CP_UTF8,0,openFile.lpstrFile,openFile.nFileOffset,nullptr,0);
        }
    }
    return ret;
}

BOOL GetOpenFileNameA(LPOPENFILENAMEA p){
    return _GetOpenFileNameA(p,Gtk::OPEN);
}

BOOL GetOpenFileNameW(LPOPENFILENAMEW p){
    return _GetOpenFileNameW(p,Gtk::OPEN);
}


BOOL GetSaveFileNameA(LPOPENFILENAMEA p)
{
    return _GetOpenFileNameA(p,Gtk::SAVE);
}
BOOL GetSaveFileNameW(LPOPENFILENAMEW p)
{
    return _GetOpenFileNameW(p,Gtk::SAVE);;
}

BOOL ChooseColorA(LPCHOOSECOLORA p)
{
    #ifdef ENABLE_GTK
    return Gtk::gtk_choose_color(p->hwndOwner,&p->rgbResult);
    #else
    return FALSE;
    #endif//ENABLE_GTK
}

BOOL ChooseColorW(LPCHOOSECOLORW p)
{
    #ifdef ENABLE_GTK
    return Gtk::gtk_choose_color(p->hwndOwner,&p->rgbResult);
    #else
    return FALSE;
    #endif//ENABLE_GTK
}

BOOL WINAPI PickFolderA(_In_ LPBROWSEINFOA lpbi){
    if(!lpbi->nMaxPath || ! lpbi->lpszPath)
        return FALSE;
    OPENFILENAMEA of={0};;
    of.nMaxFile = lpbi->nMaxPath;
    of.lpstrFile = lpbi->lpszPath;
    of.hwndOwner = lpbi->hwndOwner;
    of.lpstrInitialDir = lpbi->strlRoot;
    return _GetOpenFileNameA(&of,Gtk::FOLDER);
}

BOOL WINAPI PickFolderW(_In_ LPBROWSEINFOW lpbi){
    if(!lpbi->nMaxPath || ! lpbi->lpszPath)
        return FALSE;
    OPENFILENAMEW of={0};
    of.nMaxFile = lpbi->nMaxPath;
    of.lpstrFile = lpbi->lpszPath;
    of.hwndOwner = lpbi->hwndOwner;
    of.lpstrInitialDir = lpbi->strlRoot;
    return _GetOpenFileNameW(&of,Gtk::FOLDER);
}