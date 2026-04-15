#include <shlobj.h>
#include <sys/stat.h>
#include "enumformatetc.h"
#include "tostring.hpp"
HRESULT SHCreateStdEnumFmtEtc(UINT cfmt, const FORMATETC afmt[], IEnumFORMATETC **ppenumFormatEtc)
{
    *ppenumFormatEtc = new SEnumFormatEtc(cfmt, afmt);
    if (!(*ppenumFormatEtc))
        return E_OUTOFMEMORY;
    return S_OK;
}

BOOL WINAPI SHGetSpecialFolderPathA(HWND hwndOwner, LPSTR lpszPath,int nFolder,BOOL fCreate)
{
	const char* homeDir = getenv("HOME");
	if (!homeDir) {
		return FALSE; // HOME environment variable is required
	}

#ifdef __APPLE__
	// macOS specific paths using ~/Library structure
	if (nFolder == CSIDL_MYMUSIC) {
		sprintf(lpszPath, "%s/Music", homeDir);
	}
	else if (nFolder == CSIDL_MYVIDEO) {
		sprintf(lpszPath, "%s/Movies", homeDir);
	}
	else if (nFolder == CSIDL_MYDOCUMENTS || nFolder == CSIDL_PERSONAL) {
		sprintf(lpszPath, "%s/Documents", homeDir);
	}
	else if (nFolder == CSIDL_APPDATA) {
		// macOS uses ~/Library/Application Support for app data
		sprintf(lpszPath, "%s/Library/Application Support", homeDir);
	}
	else if (nFolder == CSIDL_DESKTOP) {
		sprintf(lpszPath, "%s/Desktop", homeDir);
	}
	else if (nFolder == CSIDL_MYPICTURES) {
		sprintf(lpszPath, "%s/Pictures", homeDir);
	}
	else if (nFolder == CSIDL_DESKTOPDIRECTORY) {
		sprintf(lpszPath, "%s/Desktop", homeDir);
	}
	else if (nFolder == CSIDL_FAVORITES) {
		sprintf(lpszPath, "%s/Favorites", homeDir);
	}
	else if (nFolder == CSIDL_STARTUP) {
		sprintf(lpszPath, "%s/Library/LaunchAgents", homeDir);
	}
	else if (nFolder == CSIDL_RECENT) {
		sprintf(lpszPath, "%s/Library/Preferences", homeDir);
	}
	else if (nFolder == CSIDL_SENDTO) {
		sprintf(lpszPath, "%s/Library/Services", homeDir);
	}
	else if (nFolder == CSIDL_STARTMENU) {
		sprintf(lpszPath, "%s/Applications", homeDir);
	}
	else if (nFolder == CSIDL_PROGRAMS) {
		sprintf(lpszPath, "%s/Applications", homeDir);
	}
	else if (nFolder == CSIDL_TEMPLATES) {
		sprintf(lpszPath, "%s/Documents/Templates", homeDir);
	}
	else if (nFolder == CSIDL_INTERNET) {
		sprintf(lpszPath, "%s/Library/Safari", homeDir);
	}
	else if (nFolder == CSIDL_FONTS) {
		sprintf(lpszPath, "%s/Library/Fonts", homeDir);
	}
	else {
		// Default fallback
		strcpy(lpszPath, homeDir);
		return FALSE;
	}
#else
	// Linux implementation using XDG Base Directory Specification
	const char* xdgDocuments = getenv("XDG_DOCUMENTS_DIR");
	const char* xdgDesktop = getenv("XDG_DESKTOP_DIR");
	const char* xdgData = getenv("XDG_DATA_HOME");
	const char* xdgCache = getenv("XDG_CACHE_HOME");
	const char* xdgPictures = getenv("XDG_PICTURES_DIR");
	const char* xdgDownload = getenv("XDG_DOWNLOAD_DIR");
	const char* xdgMusic = getenv("XDG_MUSIC_DIR");
	const char* xdgVideos = getenv("XDG_VIDEOS_DIR");
	const char* xdgConfig = getenv("XDG_CONFIG_HOME");
	
	if (nFolder == CSIDL_MYMUSIC) {
		if (xdgMusic && xdgMusic[0] != '\0') {
			strcpy(lpszPath, xdgMusic);
		} else {
			sprintf(lpszPath, "%s/Music", homeDir);
		}
	}
	else if (nFolder == CSIDL_MYVIDEO) {
		if (xdgVideos && xdgVideos[0] != '\0') {
			strcpy(lpszPath, xdgVideos);
		} else {
			sprintf(lpszPath, "%s/Videos", homeDir);
		}
	}
	else if (nFolder == CSIDL_MYDOCUMENTS || nFolder == CSIDL_PERSONAL) {
		if (xdgDocuments && xdgDocuments[0] != '\0') {
			strcpy(lpszPath, xdgDocuments);
		} else {
			sprintf(lpszPath, "%s/Documents", homeDir);
		}
	}
	else if (nFolder == CSIDL_APPDATA) {
		// XDG_DATA_HOME defaults to ~/.local/share
		if (xdgData && xdgData[0] != '\0') {
			sprintf(lpszPath, "%s", xdgData);
		} else {
			sprintf(lpszPath, "%s/.local/share", homeDir);
		}
	}
	else if (nFolder == CSIDL_DESKTOP) {
		if (xdgDesktop && xdgDesktop[0] != '\0') {
			strcpy(lpszPath, xdgDesktop);
		} else {
			sprintf(lpszPath, "%s/Desktop", homeDir);
		}
	}
	else if (nFolder == CSIDL_DESKTOPDIRECTORY) {
		if (xdgDesktop && xdgDesktop[0] != '\0') {
			strcpy(lpszPath, xdgDesktop);
		} else {
			sprintf(lpszPath, "%s/Desktop", homeDir);
		}
	}
	else if (nFolder == CSIDL_MYPICTURES) {
		if (xdgPictures && xdgPictures[0] != '\0') {
			strcpy(lpszPath, xdgPictures);
		} else {
			sprintf(lpszPath, "%s/Pictures", homeDir);
		}
	}
	else if (nFolder == CSIDL_LOCAL_APPDATA) {
		// Cache directory
		if (xdgCache && xdgCache[0] != '\0') {
			sprintf(lpszPath, "%s", xdgCache);
		} else {
			sprintf(lpszPath, "%s/.cache", homeDir);
		}
	}
	else if (nFolder == CSIDL_FAVORITES) {
		sprintf(lpszPath, "%s/.favorites", homeDir);
	}
	else if (nFolder == CSIDL_STARTUP) {
		if (xdgConfig && xdgConfig[0] != '\0') {
			sprintf(lpszPath, "%s/autostart", xdgConfig);
		} else {
			sprintf(lpszPath, "%s/.config/autostart", homeDir);
		}
	}
	else if (nFolder == CSIDL_RECENT) {
		if (xdgData && xdgData[0] != '\0') {
			sprintf(lpszPath, "%s/recently-used", xdgData);
		} else {
			sprintf(lpszPath, "%s/.local/share/recently-used", homeDir);
		}
	}
	else if (nFolder == CSIDL_SENDTO) {
		sprintf(lpszPath, "%s/.local/share/nautilus/scripts", homeDir);
	}
	else if (nFolder == CSIDL_STARTMENU) {
		if (xdgData && xdgData[0] != '\0') {
			sprintf(lpszPath, "%s/applications", xdgData);
		} else {
			sprintf(lpszPath, "%s/.local/share/applications", homeDir);
		}
	}
	else if (nFolder == CSIDL_PROGRAMS) {
		if (xdgData && xdgData[0] != '\0') {
			sprintf(lpszPath, "%s/applications", xdgData);
		} else {
			sprintf(lpszPath, "%s/.local/share/applications", homeDir);
		}
	}
	else if (nFolder == CSIDL_TEMPLATES) {
		const char* xdgTemplates = getenv("XDG_TEMPLATES_DIR");
		if (xdgTemplates && xdgTemplates[0] != '\0') {
			strcpy(lpszPath, xdgTemplates);
		} else {
			sprintf(lpszPath, "%s/Templates", homeDir);
		}
	}
	else if (nFolder == CSIDL_INTERNET) {
		// No direct equivalent on Linux
		sprintf(lpszPath, "%s", homeDir);
		return FALSE;
	}
	else if (nFolder == CSIDL_FONTS) {
		sprintf(lpszPath, "%s/.fonts", homeDir);
	}
	else {
		// Default fallback
		strcpy(lpszPath, homeDir);
		return FALSE;
	}
#endif

	// Verify directory exists, create if needed for appdata/cache
	if (fCreate) {
		struct stat st;
		if (stat(lpszPath, &st) != 0 || !S_ISDIR(st.st_mode)) {
			// Try to create the directory
			if(0!=mkdir(lpszPath, 0755))
				return FALSE; // Failed to create directory
		}
	}
	
	return TRUE;
}
BOOL WINAPI SHGetSpecialFolderPathW(HWND hwndOwner, LPWSTR lpszPath,int nFolder,BOOL fCreate){
    char szPath[MAX_PATH];
    if(!SHGetSpecialFolderPathA(hwndOwner,szPath,nFolder,fCreate))
        return FALSE;
    std::wstring  strPath;
	towstring(szPath,-1,strPath);
    wcscpy(lpszPath,strPath.c_str());
    return TRUE;
}