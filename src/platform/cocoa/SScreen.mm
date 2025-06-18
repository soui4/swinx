#import <Cocoa/Cocoa.h>
#import <IOKit/graphics/IOGraphicsLib.h>
#include <cairo.h>
#include "SScreen.h"


struct DisplayData{
  CGDirectDisplayID display;
};

struct DisplayModeData
{
    CFMutableArrayRef modes;
};

static bool HasValidDisplayModeFlags(CGDisplayModeRef vidmode)
{
    uint32_t ioflags = CGDisplayModeGetIOFlags(vidmode);

    /* Filter out modes which have flags that we don't want. */
    if (ioflags & (kDisplayModeNeverShowFlag | kDisplayModeNotGraphicsQualityFlag)) {
        return FALSE;
    }

    /* Filter out modes which don't have flags that we want. */
    if (!(ioflags & kDisplayModeValidFlag) || !(ioflags & kDisplayModeSafeFlag)) {
        return FALSE;
    }

    return TRUE;
}

static uint32_t GetDisplayModePixelFormat(CGDisplayModeRef vidmode)
{
    /* This API is deprecated in 10.11 with no good replacement (as of 10.15). */
    CFStringRef fmt = CGDisplayModeCopyPixelEncoding(vidmode);
    uint32_t pixelformat = CAIRO_FORMAT_INVALID ;

    if (CFStringCompare(fmt, CFSTR(IO32BitDirectPixels),
                        kCFCompareCaseInsensitive) == kCFCompareEqualTo) {
        pixelformat = CAIRO_FORMAT_ARGB32;
    } else if (CFStringCompare(fmt, CFSTR(IO16BitDirectPixels),
                        kCFCompareCaseInsensitive) == kCFCompareEqualTo) {
        pixelformat = CAIRO_FORMAT_RGB16_565;//SDL_PIXELFORMAT_ARGB1555;
    } else if (CFStringCompare(fmt, CFSTR(kIO30BitDirectPixels),
                        kCFCompareCaseInsensitive) == kCFCompareEqualTo) {
        pixelformat = CAIRO_FORMAT_RGB30;
    } else {
        /* ignore 8-bit and such for now. */
    }

    CFRelease(fmt);

    return pixelformat;
}


static int GetDisplayModeRefreshRate(CGDisplayModeRef vidmode, CVDisplayLinkRef link)
{
    int refreshRate = (int) (CGDisplayModeGetRefreshRate(vidmode) + 0.5);

    /* CGDisplayModeGetRefreshRate can return 0 (eg for built-in displays). */
    if (refreshRate == 0 && link != NULL) {
        CVTime time = CVDisplayLinkGetNominalOutputVideoRefreshPeriod(link);
        if ((time.flags & kCVTimeIsIndefinite) == 0 && time.timeValue != 0) {
            refreshRate = (int) ((time.timeScale / (double) time.timeValue) + 0.5);
        }
    }

    return refreshRate;
}

static bool GetDisplayMode(CGDisplayModeRef vidmode, bool vidmodeCurrent, CFArrayRef modelist, CVDisplayLinkRef link, DisplayMode *mode)
{
    bool usableForGUI = CGDisplayModeIsUsableForDesktopGUI(vidmode);
    int width = (int) CGDisplayModeGetWidth(vidmode);
    int height = (int) CGDisplayModeGetHeight(vidmode);
    uint32_t ioflags = CGDisplayModeGetIOFlags(vidmode);
    int refreshrate = GetDisplayModeRefreshRate(vidmode, link);
    uint32_t format = GetDisplayModePixelFormat(vidmode);
    bool interlaced = (ioflags & kDisplayModeInterlacedFlag) != 0;
    CFMutableArrayRef modes;

    if (format == CAIRO_FORMAT_INVALID) {
        return FALSE;
    }

    /* Don't fail the current mode based on flags because this could prevent Cocoa_InitModes from
     * succeeding if the current mode lacks certain flags (esp kDisplayModeSafeFlag). */
    if (!vidmodeCurrent && !HasValidDisplayModeFlags(vidmode)) {
        return FALSE;
    }

    modes = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    CFArrayAppendValue(modes, vidmode);

    /* If a list of possible diplay modes is passed in, use it to filter out
     * modes that have duplicate sizes. We don't just rely on SDL's higher level
     * duplicate filtering because this code can choose what properties are
     * prefered, and it can add CGDisplayModes to the DisplayModeData's list of
     * modes to try (see comment below for why that's necessary).
     * CGDisplayModeGetPixelWidth and friends are only available in 10.8+. */
#ifdef MAC_OS_X_VERSION_10_8
    if (modelist != NULL && floor(NSAppKitVersionNumber) > NSAppKitVersionNumber10_7) {
        int pixelW = (int) CGDisplayModeGetPixelWidth(vidmode);
        int pixelH = (int) CGDisplayModeGetPixelHeight(vidmode);

        CFIndex modescount = CFArrayGetCount(modelist);
        int  i;

        for (i = 0; i < modescount; i++) {
            int otherW, otherH, otherpixelW, otherpixelH, otherrefresh;
            uint32_t otherformat;
            bool otherGUI;
            CGDisplayModeRef othermode = (CGDisplayModeRef) CFArrayGetValueAtIndex(modelist, i);
            uint32_t otherioflags = CGDisplayModeGetIOFlags(othermode);

            if (CFEqual(vidmode, othermode)) {
                continue;
            }

            if (!HasValidDisplayModeFlags(othermode)) {
                continue;
            }

            otherW = (int) CGDisplayModeGetWidth(othermode);
            otherH = (int) CGDisplayModeGetHeight(othermode);
            otherpixelW = (int) CGDisplayModeGetPixelWidth(othermode);
            otherpixelH = (int) CGDisplayModeGetPixelHeight(othermode);
            otherrefresh = GetDisplayModeRefreshRate(othermode, link);
            otherformat = GetDisplayModePixelFormat(othermode);
            otherGUI = CGDisplayModeIsUsableForDesktopGUI(othermode);

            /* Ignore this mode if it's low-dpi (@1x) and we have a high-dpi
             * mode in the list with the same size in points.
             */
            if (width == pixelW && height == pixelH
                && width == otherW && height == otherH
                && refreshrate == otherrefresh && format == otherformat
                && (otherpixelW != otherW || otherpixelH != otherH)) {
                CFRelease(modes);
                return FALSE;
            }

            /* Ignore this mode if it's interlaced and there's a non-interlaced
             * mode in the list with the same properties.
             */
            if (interlaced && ((otherioflags & kDisplayModeInterlacedFlag) == 0)
                && width == otherW && height == otherH && pixelW == otherpixelW
                && pixelH == otherpixelH && refreshrate == otherrefresh
                && format == otherformat && usableForGUI == otherGUI) {
                CFRelease(modes);
                return FALSE;
            }

            /* Ignore this mode if it's not usable for desktop UI and its
             * properties are equal to another GUI-capable mode in the list.
             */
            if (width == otherW && height == otherH && pixelW == otherpixelW
                && pixelH == otherpixelH && !usableForGUI && otherGUI
                && refreshrate == otherrefresh && format == otherformat) {
                CFRelease(modes);
                return FALSE;
            }

            /* If multiple modes have the exact same properties, they'll all
             * go in the list of modes to try when SetDisplayMode is called.
             * This is needed because kCGDisplayShowDuplicateLowResolutionModes
             * (which is used to expose highdpi display modes) can make the
             * list of modes contain duplicates (according to their properties
             * obtained via public APIs) which don't work with SetDisplayMode.
             * Those duplicate non-functional modes *do* have different pixel
             * formats according to their internal data structure viewed with
             * NSLog, but currently no public API can detect that.
             * https://bugzilla.libsdl.org/show_bug.cgi?id=4822
             *
             * As of macOS 10.15.0, those duplicates have the exact same
             * properties via public APIs in every way (even their IO flags and
             * CGDisplayModeGetIODisplayModeID is the same), so we could test
             * those for equality here too, but I'm intentionally not doing that
             * in case there are duplicate modes with different IO flags or IO
             * display mode IDs in the future. In that case I think it's better
             * to try them all in SetDisplayMode than to risk one of them being
             * correct but it being filtered out by SDL_AddDisplayMode as being
             * a duplicate.
             */
            if (width == otherW && height == otherH && pixelW == otherpixelW
                && pixelH == otherpixelH && usableForGUI == otherGUI
                && refreshrate == otherrefresh && format == otherformat) {
                CFArrayAppendValue(modes, othermode);
            }
        }
    }
#endif
    std::shared_ptr<DisplayModeData> data = std::make_shared<DisplayModeData>();
    data->modes = modes;
    mode->format = format;
    mode->w = width;
    mode->h = height;
    mode->refresh_rate = refreshrate;
    mode->driverdata = data;
    return TRUE;
}


const char *Cocoa_GetDisplayName(uint32_t displayID)
{
    /* This API is deprecated in 10.9 with no good replacement (as of 10.15). */
    io_service_t servicePort = CGDisplayIOServicePort(displayID);
    CFDictionaryRef deviceInfo = IODisplayCreateInfoDictionary(servicePort, kIODisplayOnlyPreferredName);
    NSDictionary *localizedNames = [(__bridge NSDictionary *)deviceInfo objectForKey:[NSString stringWithUTF8String:kDisplayProductName]];
    const char* displayName = NULL;

    if ([localizedNames count] > 0) {
        displayName = strdup([[localizedNames objectForKey:[[localizedNames allKeys] objectAtIndex:0]] UTF8String]);
    }
    CFRelease(deviceInfo);
    return displayName;
}

int Cocoa_GetDisplayBounds(VideoDisplay * display, RECT * rect)
{
    DisplayData *displaydata = display->driverdata.get();
    CGRect cgrect;

    cgrect = CGDisplayBounds(displaydata->display);
    rect->left = (int)cgrect.origin.x;
    rect->top = (int)cgrect.origin.y;
    rect->right = rect->left+ (int)cgrect.size.width;
    rect->right = rect->top + (int)cgrect.size.height;
    return 0;
}


uint32_t Cocoa_InitDisplayModes(std::vector<VideoDisplay> * pDisplays)
{
@autoreleasepool
{
    CGDisplayErr result;
    CGDisplayCount numDisplays;
    int pass, i;

    result = CGGetOnlineDisplayList(0, NULL, &numDisplays);
    if (result != kCGErrorSuccess) {
        //CG_SetError("CGGetOnlineDisplayList()", result);
        return 0;
    }
    std::vector<CGDirectDisplayID> displays;
    displays.resize(numDisplays);
    result = CGGetOnlineDisplayList(numDisplays, displays.data(), &numDisplays);
    if (result != kCGErrorSuccess) {
        return 0;
    }

    /* Pick up the primary display in the first pass, then get the rest */
    for (pass = 0; pass < 2; ++pass) {
        for (i = 0; i < numDisplays; ++i) {
            VideoDisplay display;
            DisplayMode mode;
            CGDisplayModeRef moderef = NULL;
            CVDisplayLinkRef link = NULL;

            if (pass == 0) {
                if (!CGDisplayIsMain(displays[i])) {
                    continue;
                }
            } else {
                if (CGDisplayIsMain(displays[i])) {
                    continue;
                }
            }

            if (CGDisplayMirrorsDisplay(displays[i]) != kCGNullDirectDisplay) {
                continue;
            }

            moderef = CGDisplayCopyDisplayMode(displays[i]);

            if (!moderef) {
                continue;
            }
            std::shared_ptr<DisplayData> displaydata = std::make_shared<DisplayData>();
            displaydata->display = displays[i];

            CVDisplayLinkCreateWithCGDisplay(displays[i], &link);

            memset(&display,0,sizeof(display));
            /* this returns a stddup'ed string */
            const char * name = Cocoa_GetDisplayName(displays[i]);
            if(name)
                display.name = name;// swinx::Cocoa_GetDisplayName(displays[i]);
            if (!GetDisplayMode(moderef, TRUE, NULL, link, &mode)) {
                CVDisplayLinkRelease(link);
                CGDisplayModeRelease(moderef);
                continue;
            }

            CVDisplayLinkRelease(link);
            CGDisplayModeRelease(moderef);

            display.desktop_mode = i;
            display.current_mode = i;
            display.driverdata = displaydata;
            display.display_modes.push_back(mode);
            pDisplays->push_back(display);
        }
    }
    return pDisplays->size();
    }
}