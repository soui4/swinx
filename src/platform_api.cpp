#include "platform_api.h"
#include <stdlib.h>

struct PlatformAPI g_platformAPI = {
    .version = PLATFORM_API_VERSION,
    .clipboard = {
        .openClipboard = NULL,
        .closeClipboard = NULL,
        .emptyClipboard = NULL,
        .getClipboardData = NULL,
        .setClipboardData = NULL,
        .isClipboardFormatAvailable = NULL,
        .registerClipboardFormat = NULL,
        .getClipboardOwner = NULL,
        .hasFormat = NULL,
    },
    .window = {
            .getWindow=NULL,
        .createWindow = NULL,
        .destroyWindow = NULL,
        .moveWindow = NULL,
        .setWindowPos = NULL,
        .setWindowSize = NULL,
        .setWindowPosEx = NULL,
        .invalidRect = NULL,
        .isWindowVisible = NULL,
        .showWindow = NULL,
        .enableWindow = NULL,
        .isWindowEnabled = NULL,
        .getForegroundWindow = NULL,
        .setForegroundWindow = NULL,
        .getFocus = NULL,
        .setFocus = NULL,
        .getScreenWidth = NULL,
        .getScreenHeight = NULL,
        .getWorkArea = NULL,
        .setCapture = NULL,
        .releaseCapture=NULL,
        .getDpi = NULL,
        .getCursorPos = NULL,
        .getRawInputDeviceList=NULL,
        .getRawInputDeviceInfoA=NULL,
        .getRawInputDeviceInfoW=NULL,
        .showSoftKeyboard = NULL,
    },
    .ime = {
        .immCreateContext = NULL,
        .immDestroyContext = NULL,
        .immGetContext = NULL,
        .immAssociateContext = NULL,
        .immReleaseContext = NULL,
        .immGetCompositionStringA = NULL,
        .immGetCompositionStringW = NULL,
        .immSetCompositionStringA = NULL,
        .immSetCompositionStringW = NULL,
        .immNotifyIME = NULL,
        .immGetConversionStatus = NULL,
        .immSetConversionStatus = NULL,
        .immGetOpenStatus = NULL,
        .immSetOpenStatus = NULL,
        .immGetStatusWindowPos = NULL,
        .immSetStatusWindowPos = NULL,
        .immGetCompositionWindow = NULL,
        .immSetCompositionWindow = NULL,
        .immGetCandidateWindow = NULL,
        .immSetCandidateWindow = NULL,
        .immEscapeA = NULL,
        .immEscapeW = NULL,
        .immSetCompositionFontA = NULL,
        .immSetCompositionFontW = NULL,
        .immGetProperty = NULL,
        .immGetVirtualKey = NULL,
        .immGetDefaultIMEWnd = NULL,
        .immIsIME = NULL,
    },
};

BOOL PlatformAPI_Init(struct PlatformAPI *api)
{
    if (!api || api->version != PLATFORM_API_VERSION)
        return FALSE;

    g_platformAPI = *api;
    return TRUE;
}

void PlatformAPI_Deinit(void)
{
    g_platformAPI.version = PLATFORM_API_VERSION;
    g_platformAPI.clipboard.openClipboard = NULL;
    g_platformAPI.clipboard.closeClipboard = NULL;
    g_platformAPI.clipboard.emptyClipboard = NULL;
    g_platformAPI.clipboard.getClipboardData = NULL;
    g_platformAPI.clipboard.setClipboardData = NULL;
    g_platformAPI.clipboard.isClipboardFormatAvailable = NULL;
    g_platformAPI.clipboard.registerClipboardFormat = NULL;
    g_platformAPI.clipboard.getClipboardOwner = NULL;
    g_platformAPI.clipboard.hasFormat = NULL;
    
    g_platformAPI.window.getWindow = NULL;
    g_platformAPI.window.createWindow = NULL;
    g_platformAPI.window.destroyWindow = NULL;
    g_platformAPI.window.moveWindow = NULL;
    g_platformAPI.window.setWindowPos = NULL;
    g_platformAPI.window.setWindowSize = NULL;
    g_platformAPI.window.setWindowPosEx = NULL;
    g_platformAPI.window.isWindowVisible = NULL;
    g_platformAPI.window.showWindow = NULL;
    g_platformAPI.window.enableWindow = NULL;
    g_platformAPI.window.isWindowEnabled = NULL;
    g_platformAPI.window.getForegroundWindow = NULL;
    g_platformAPI.window.setForegroundWindow = NULL;
    g_platformAPI.window.getFocus = NULL;
    g_platformAPI.window.setFocus = NULL;
    g_platformAPI.window.getScreenWidth = NULL;
    g_platformAPI.window.getScreenHeight = NULL;
    g_platformAPI.window.getWorkArea = NULL;
    g_platformAPI.window.setCapture = NULL;
    g_platformAPI.window.releaseCapture = NULL;
    g_platformAPI.window.getCursorPos = NULL;
    g_platformAPI.window.getDpi = NULL;
    g_platformAPI.ime.immCreateContext = NULL;
    g_platformAPI.ime.immDestroyContext = NULL;
    g_platformAPI.ime.immGetContext = NULL;
    g_platformAPI.ime.immAssociateContext = NULL;
    g_platformAPI.ime.immReleaseContext = NULL;
    g_platformAPI.ime.immGetCompositionStringA = NULL;
    g_platformAPI.ime.immGetCompositionStringW = NULL;
    g_platformAPI.ime.immSetCompositionStringA = NULL;
    g_platformAPI.ime.immSetCompositionStringW = NULL;
    g_platformAPI.ime.immNotifyIME = NULL;
    g_platformAPI.ime.immGetConversionStatus = NULL;
    g_platformAPI.ime.immSetConversionStatus = NULL;
    g_platformAPI.ime.immGetOpenStatus = NULL;
    g_platformAPI.ime.immSetOpenStatus = NULL;
    g_platformAPI.ime.immGetStatusWindowPos = NULL;
    g_platformAPI.ime.immSetStatusWindowPos = NULL;
    g_platformAPI.ime.immGetCompositionWindow = NULL;
    g_platformAPI.ime.immSetCompositionWindow = NULL;
    g_platformAPI.ime.immGetCandidateWindow = NULL;
    g_platformAPI.ime.immSetCandidateWindow = NULL;
    g_platformAPI.ime.immEscapeA = NULL;
    g_platformAPI.ime.immEscapeW = NULL;
    g_platformAPI.ime.immSetCompositionFontA = NULL;
    g_platformAPI.ime.immSetCompositionFontW = NULL;
    g_platformAPI.ime.immGetProperty = NULL;
    g_platformAPI.ime.immGetVirtualKey = NULL;
    g_platformAPI.ime.immGetDefaultIMEWnd = NULL;
    g_platformAPI.ime.immIsIME = NULL;
}
