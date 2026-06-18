#include "SOhosWindow.h"
#include "SConnBase.h"
#include "napi_bridge.h"
#include <gdi.h>
#include <winuser.h>
#include <wnd.h>
#include <map>
#include <mutex>
#include <string>
#include <algorithm>
#include <cstring>
#include <vector>

#include <hilog/log.h>

static const unsigned int LOG_DOMAIN_SOUI_WND = 0x5349;
static const char *LOG_TAG_SOUI_WND = "SOUI_OHOS_WND";

#define SOUI_WND_LOGI(fmt, ...) OH_LOG_Print(LOG_APP, LOG_INFO, LOG_DOMAIN_SOUI_WND, LOG_TAG_SOUI_WND, fmt, ##__VA_ARGS__)
#define SOUI_WND_LOGW(fmt, ...) OH_LOG_Print(LOG_APP, LOG_WARN, LOG_DOMAIN_SOUI_WND, LOG_TAG_SOUI_WND, fmt, ##__VA_ARGS__)

struct SOhosWindow {
    RECT rect;
    HWND parent;
    HWND owner;
    DWORD style;
    DWORD exStyle;
    BOOL visible;
    BOOL enabled;
    BOOL transparent;
    BYTE alpha;
    HCURSOR cursor;
    SConnBase *listener;
    std::string title;
    cairo_surface_t *surface;
};

static std::recursive_mutex s_mutex;
static std::map<HWND, SOhosWindow *> s_windows;
static std::vector<HWND> s_zOrder;
static HWND s_activeWindow = 0;
static HWND s_focusWindow = 0;
static HWND s_captureWindow = 0;
static HWND s_mainWindowMoveWindow = 0;
static HWND s_mainWindowResizeWindow = 0;
static POINT s_cursorPos = { 0, 0 };
static WORD s_cursorId = 0;

extern WORD GetCursorID(HICON hIcon);

static SOhosWindow *fromHwnd(HWND hWnd)
{
    auto it = s_windows.find(hWnd);
    return it == s_windows.end() ? nullptr : it->second;
}

static bool shouldTraceWindowLocked(const SOhosWindow *wnd)
{
    return wnd && (!wnd->parent || wnd->owner);
}

static int rectWidth(const RECT &rc)
{
    return std::max(0, rc.right - rc.left);
}

static int rectHeight(const RECT &rc)
{
    return std::max(0, rc.bottom - rc.top);
}

static bool isChildOfLocked(HWND hWnd, HWND parent)
{
    SOhosWindow *wnd = fromHwnd(hWnd);
    while (wnd && wnd->parent)
    {
        if (wnd->parent == parent)
            return true;
        wnd = fromHwnd(wnd->parent);
    }
    return false;
}

static bool isTopLevelLocked(HWND hWnd)
{
    SOhosWindow *wnd = fromHwnd(hWnd);
    return wnd && !wnd->parent;
}

static HWND rootWindowLocked(HWND hWnd)
{
    HWND root = hWnd;
    SOhosWindow *wnd = fromHwnd(hWnd);
    while (wnd && wnd->parent && fromHwnd(wnd->parent))
    {
        root = wnd->parent;
        wnd = fromHwnd(root);
    }
    return root;
}

static HWND findPresentWindowLocked(HWND preferred)
{
    HWND preferredRoot = rootWindowLocked(preferred);
    SOhosWindow *preferredWnd = fromHwnd(preferredRoot);
    if (preferredWnd && isTopLevelLocked(preferredRoot) && !preferredWnd->owner && preferredWnd->surface &&
        cairo_surface_status(preferredWnd->surface) == CAIRO_STATUS_SUCCESS)
        return preferredRoot;

    for (HWND hWnd : s_zOrder)
    {
        SOhosWindow *wnd = fromHwnd(hWnd);
        if (wnd && !wnd->parent && !wnd->owner && wnd->surface && cairo_surface_status(wnd->surface) == CAIRO_STATUS_SUCCESS)
            return hWnd;
    }
    for (HWND hWnd : s_zOrder)
    {
        SOhosWindow *wnd = fromHwnd(hWnd);
        if (wnd && !wnd->parent && wnd->surface && cairo_surface_status(wnd->surface) == CAIRO_STATUS_SUCCESS)
            return hWnd;
    }
    return 0;
}

static bool absoluteRectLocked(HWND hWnd, RECT *rc)
{
    if (!rc)
        return false;
    SOhosWindow *wnd = fromHwnd(hWnd);
    if (!wnd)
        return false;
    *rc = wnd->rect;
    HWND parent = wnd->parent;
    while (parent)
    {
        SOhosWindow *parentWnd = fromHwnd(parent);
        if (!parentWnd)
            break;
        OffsetRect(rc, parentWnd->rect.left, parentWnd->rect.top);
        parent = parentWnd->parent;
    }
    return true;
}

static bool visibleRectLocked(HWND hWnd, RECT *rc)
{
    if (!absoluteRectLocked(hWnd, rc))
        return false;
    SOhosWindow *wnd = fromHwnd(hWnd);
    while (wnd)
    {
        if (!wnd->visible)
            return false;
        HWND parent = wnd->parent;
        if (!parent)
            break;
        RECT parentRc;
        if (!absoluteRectLocked(parent, &parentRc) || !IntersectRect(rc, rc, &parentRc))
            return false;
        wnd = fromHwnd(parent);
    }
    return !IsRectEmpty(rc);
}

static void removeFromZOrderLocked(HWND hWnd)
{
    s_zOrder.erase(std::remove(s_zOrder.begin(), s_zOrder.end(), hWnd), s_zOrder.end());
}

static bool isValidImageSurface(cairo_surface_t *surface)
{
    return surface && cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS &&
        cairo_surface_get_type(surface) == CAIRO_SURFACE_TYPE_IMAGE;
}

struct SurfaceStats {
    int maxAlpha;
    int maxColor;
};

static SurfaceStats getSurfaceStats(cairo_surface_t *surface)
{
    SurfaceStats stats = { 0, 0 };
    if (!isValidImageSurface(surface))
        return stats;

    cairo_surface_flush(surface);
    const unsigned char *data = cairo_image_surface_get_data(surface);
    int width = cairo_image_surface_get_width(surface);
    int height = cairo_image_surface_get_height(surface);
    int stride = cairo_image_surface_get_stride(surface);
    int yStep = std::max(1, height / 64);
    int xStep = std::max(1, width / 64);
    for (int y = 0; y < height; y += yStep)
    {
        const unsigned char *row = data + y * stride;
        for (int x = 0; x < width; x += xStep)
        {
            const unsigned char *px = row + x * 4;
            stats.maxColor = std::max<int>(stats.maxColor, std::max<int>(px[0], std::max<int>(px[1], px[2])));
            stats.maxAlpha = std::max<int>(stats.maxAlpha, px[3]);
        }
    }
    return stats;
}

static void drawWindowSurfaceLocked(cairo_t *cr, HWND hWnd, const RECT &rootAbs)
{
    SOhosWindow *wnd = fromHwnd(hWnd);
    if (!wnd || !isValidImageSurface(wnd->surface))
        return;
    RECT visible;
    if (!visibleRectLocked(hWnd, &visible))
        return;
    RECT wndAbs;
    if (!absoluteRectLocked(hWnd, &wndAbs))
        return;
    cairo_surface_flush(wnd->surface);
    cairo_save(cr);
    cairo_rectangle(cr, visible.left - rootAbs.left, visible.top - rootAbs.top, rectWidth(visible), rectHeight(visible));
    cairo_clip(cr);
    cairo_set_source_surface(cr, wnd->surface, wndAbs.left - rootAbs.left, wndAbs.top - rootAbs.top);
    if (wnd->alpha == 0xff)
        cairo_paint(cr);
    else
        cairo_paint_with_alpha(cr, wnd->alpha / 255.0);
    cairo_restore(cr);
}

static void drawWindowTreeLocked(cairo_t *cr, HWND hWnd, const RECT &rootAbs)
{
    drawWindowSurfaceLocked(cr, hWnd, rootAbs);
    for (HWND child : s_zOrder)
    {
        SOhosWindow *childWnd = fromHwnd(child);
        if (childWnd && childWnd->parent == hWnd)
            drawWindowTreeLocked(cr, child, rootAbs);
    }
}

static bool hasCompositedOverlayLocked()
{
    for (HWND hWnd : s_zOrder)
    {
        SOhosWindow *wnd = fromHwnd(hWnd);
        if (wnd && !wnd->parent && wnd->owner && wnd->visible && isValidImageSurface(wnd->surface))
            return true;
    }
    return false;
}

static bool normalizeOpaqueIfAlphaEmpty(cairo_surface_t *surface)
{
    if (!isValidImageSurface(surface))
        return false;

    SurfaceStats stats = getSurfaceStats(surface);
    if (stats.maxAlpha != 0 || stats.maxColor == 0)
        return false;

    int width = cairo_image_surface_get_width(surface);
    int height = cairo_image_surface_get_height(surface);
    int stride = cairo_image_surface_get_stride(surface);
    unsigned char *data = cairo_image_surface_get_data(surface);
    for (int y = 0; y < height; ++y)
    {
        unsigned char *row = data + y * stride;
        for (int x = 0; x < width; ++x)
            row[x * 4 + 3] = 0xff;
    }
    cairo_surface_mark_dirty(surface);
    return true;
}

static void forceSurfaceOpaque(cairo_surface_t *surface)
{
    if (!isValidImageSurface(surface))
        return;

    cairo_surface_flush(surface);
    unsigned char *data = cairo_image_surface_get_data(surface);
    int width = cairo_image_surface_get_width(surface);
    int height = cairo_image_surface_get_height(surface);
    int stride = cairo_image_surface_get_stride(surface);
    for (int y = 0; y < height; ++y)
    {
        unsigned char *row = data + y * stride;
        for (int x = 0; x < width; ++x)
            row[x * 4 + 3] = 0xff;
    }
    cairo_surface_mark_dirty(surface);
}

static cairo_surface_t *cloneWindowSurface(cairo_surface_t *source, bool *alphaFixed)
{
    if (alphaFixed)
        *alphaFixed = false;
    if (!isValidImageSurface(source))
        return nullptr;

    cairo_surface_flush(source);
    int width = cairo_image_surface_get_width(source);
    int height = cairo_image_surface_get_height(source);
    if (width <= 0 || height <= 0)
        return nullptr;

    cairo_surface_t *copy = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (!copy || cairo_surface_status(copy) != CAIRO_STATUS_SUCCESS)
    {
        if (copy)
            cairo_surface_destroy(copy);
        return nullptr;
    }

    const unsigned char *src = cairo_image_surface_get_data(source);
    unsigned char *dst = cairo_image_surface_get_data(copy);
    int srcStride = cairo_image_surface_get_stride(source);
    int dstStride = cairo_image_surface_get_stride(copy);
    int rowBytes = width * 4;
    for (int y = 0; y < height; ++y)
        memcpy(dst + y * dstStride, src + y * srcStride, rowBytes);
    cairo_surface_mark_dirty(copy);

    bool fixed = normalizeOpaqueIfAlphaEmpty(copy);
    if (alphaFixed)
        *alphaFixed = fixed;
    return copy;
}

static bool presentCompositedWindowLocked(HWND hWnd, const RECT *)
{
    HWND presentWnd = findPresentWindowLocked(hWnd);
    SOhosWindow *present = fromHwnd(presentWnd);
    if (!present || !present->surface || cairo_surface_status(present->surface) != CAIRO_STATUS_SUCCESS)
    {
        SOhosWindow *wnd = fromHwnd(hWnd);
        if (shouldTraceWindowLocked(wnd))
            SOUI_WND_LOGW("present skipped hwnd=%{public}p present=%{public}p surface=%{public}p",
                          reinterpret_cast<void *>(hWnd), reinterpret_cast<void *>(presentWnd),
                          present ? reinterpret_cast<void *>(present->surface) : nullptr);
        return false;
    }

    RECT rootAbs;
    if (!absoluteRectLocked(presentWnd, &rootAbs))
        return false;
    int width = cairo_image_surface_get_width(present->surface);
    int height = cairo_image_surface_get_height(present->surface);
    if (width <= 0 || height <= 0)
    {
        SOUI_WND_LOGW("present skipped invalid root surface hwnd=%{public}p size=%{public}dx%{public}d",
                      reinterpret_cast<void *>(presentWnd), width, height);
        return false;
    }

    if (!hasCompositedOverlayLocked())
    {
        forceSurfaceOpaque(present->surface);
        return swinx::ohos::PresentCairoSurface(present->surface, nullptr);
    }

    cairo_surface_t *composited = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (!composited || cairo_surface_status(composited) != CAIRO_STATUS_SUCCESS)
    {
        if (composited)
            cairo_surface_destroy(composited);
        return false;
    }

    cairo_t *cr = cairo_create(composited);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    for (HWND top : s_zOrder)
    {
        SOhosWindow *topWnd = fromHwnd(top);
        if (topWnd && !topWnd->parent)
            drawWindowTreeLocked(cr, top, rootAbs);
    }

    cairo_destroy(cr);
    cairo_surface_flush(composited);
    forceSurfaceOpaque(composited);

    bool ok = swinx::ohos::PresentCairoSurface(composited, nullptr);
    SOhosWindow *wnd = fromHwnd(hWnd);
    if (shouldTraceWindowLocked(wnd) && wnd->owner)
        SOUI_WND_LOGI("present hwnd=%{public}p owner=%{public}p root=%{public}p rootSize=%{public}dx%{public}d ok=%{public}d",
                      reinterpret_cast<void *>(hWnd), reinterpret_cast<void *>(wnd->owner), reinterpret_cast<void *>(presentWnd),
                      width, height, ok ? 1 : 0);
    cairo_surface_destroy(composited);
    return ok;
}

HWND createOhosWindow(HWND hParent, DWORD dwStyle, DWORD dwExStyle, BOOL, LPCSTR pszTitle, int x, int y, int cx, int cy, SConnBase *listener)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    SOhosWindow *wnd = new SOhosWindow;
    wnd->rect = { x, y, x + std::max(cx, 1), y + std::max(cy, 1) };
    wnd->parent = (dwStyle & WS_CHILD) ? hParent : 0;
    wnd->owner = (dwStyle & WS_CHILD) ? 0 : hParent;
    wnd->style = dwStyle;
    wnd->exStyle = dwExStyle;
    wnd->visible = FALSE;
    wnd->enabled = TRUE;
    wnd->transparent = FALSE;
    wnd->alpha = 0xff;
    wnd->cursor = 0;
    wnd->listener = listener;
    wnd->surface = nullptr;
    if (pszTitle)
        wnd->title = pszTitle;
    HWND hWnd = reinterpret_cast<HWND>(wnd);
    s_windows[hWnd] = wnd;
    s_zOrder.push_back(hWnd);
    SOUI_WND_LOGI("create hwnd=%{public}p parent=%{public}p owner=%{public}p style=%{public}x ex=%{public}x rect=%{public}d,%{public}d,%{public}d,%{public}d title=%{public}s",
                  reinterpret_cast<void *>(hWnd), reinterpret_cast<void *>(wnd->parent), reinterpret_cast<void *>(wnd->owner),
                  static_cast<unsigned int>(dwStyle), static_cast<unsigned int>(dwExStyle), wnd->rect.left, wnd->rect.top,
                  wnd->rect.right, wnd->rect.bottom, pszTitle ? pszTitle : "");
    return hWnd;
}

void closeOhosWindow(HWND hWnd)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    auto it = s_windows.find(hWnd);
    if (it == s_windows.end())
        return;
    if (s_activeWindow == hWnd)
        s_activeWindow = 0;
    if (s_focusWindow == hWnd)
        s_focusWindow = 0;
    if (s_captureWindow == hWnd)
        s_captureWindow = 0;
    HWND presentWnd = findPresentWindowLocked(hWnd);
    SOUI_WND_LOGI("close hwnd=%{public}p present=%{public}p", reinterpret_cast<void *>(hWnd), reinterpret_cast<void *>(presentWnd));
    for (auto &entry : s_windows)
    {
        if (entry.second && entry.second->parent == hWnd)
            entry.second->parent = 0;
        if (entry.second && entry.second->owner == hWnd)
            entry.second->owner = 0;
    }
    if (it->second->surface)
        cairo_surface_destroy(it->second->surface);
    removeFromZOrderLocked(hWnd);
    delete it->second;
    s_windows.erase(it);
    if (presentWnd)
        presentCompositedWindowLocked(presentWnd, nullptr);
}

BOOL showOhosWindow(HWND hWnd, int nCmdShow)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    SOhosWindow *wnd = fromHwnd(hWnd);
    if (!wnd)
        return FALSE;
    wnd->visible = nCmdShow != SW_HIDE;
    SOUI_WND_LOGI("show hwnd=%{public}p cmd=%{public}d visible=%{public}d parent=%{public}p owner=%{public}p",
                  reinterpret_cast<void *>(hWnd), nCmdShow, wnd->visible ? 1 : 0, reinterpret_cast<void *>(wnd->parent),
                  reinterpret_cast<void *>(wnd->owner));
    if (wnd->visible)
        setOhosWindowZorder(hWnd, HWND_TOP);
    else
        presentCompositedWindowLocked(hWnd, nullptr);
    return TRUE;
}

BOOL setOhosWindowPos(HWND hWnd, int x, int y)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    SOhosWindow *wnd = fromHwnd(hWnd);
    if (!wnd)
        return FALSE;
    int oldX = wnd->rect.left;
    int oldY = wnd->rect.top;
    int cx = wnd->rect.right - wnd->rect.left;
    int cy = wnd->rect.bottom - wnd->rect.top;
    wnd->rect = { x, y, x + cx, y + cy };
    if (!wnd->parent && !wnd->owner && s_mainWindowMoveWindow == hWnd)
        swinx::ohos::NotifyMainWindowMoveDelta(x - oldX, y - oldY);
    if (shouldTraceWindowLocked(wnd))
        SOUI_WND_LOGI("setPos hwnd=%{public}p owner=%{public}p pos=%{public}d,%{public}d size=%{public}dx%{public}d",
                      reinterpret_cast<void *>(hWnd), reinterpret_cast<void *>(wnd->owner), x, y, cx, cy);
    return TRUE;
}

void beginOhosMainWindowMove(HWND hWnd)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    SOhosWindow *wnd = fromHwnd(hWnd);
    if (wnd && !wnd->parent && !wnd->owner)
        s_mainWindowMoveWindow = hWnd;
}

void endOhosMainWindowMove(HWND hWnd)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    if (!hWnd || s_mainWindowMoveWindow == hWnd)
        s_mainWindowMoveWindow = 0;
}

void beginOhosMainWindowResize(HWND hWnd)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    SOhosWindow *wnd = fromHwnd(hWnd);
    if (wnd && !wnd->parent && !wnd->owner)
        s_mainWindowResizeWindow = hWnd;
}

void endOhosMainWindowResize(HWND hWnd)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    if (!hWnd || s_mainWindowResizeWindow == hWnd)
        s_mainWindowResizeWindow = 0;
}

BOOL setOhosWindowSize(HWND hWnd, int cx, int cy)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    SOhosWindow *wnd = fromHwnd(hWnd);
    if (!wnd)
        return FALSE;
    wnd->rect.right = wnd->rect.left + std::max(cx, 1);
    wnd->rect.bottom = wnd->rect.top + std::max(cy, 1);
    if (!wnd->parent && !wnd->owner && s_mainWindowResizeWindow == hWnd)
    {
        swinx::ohos::NotifyMainWindowRect(wnd->rect.left, wnd->rect.top, rectWidth(wnd->rect), rectHeight(wnd->rect));
    }
    if (shouldTraceWindowLocked(wnd))
        SOUI_WND_LOGI("setSize hwnd=%{public}p owner=%{public}p pos=%{public}d,%{public}d size=%{public}dx%{public}d",
                      reinterpret_cast<void *>(hWnd), reinterpret_cast<void *>(wnd->owner), wnd->rect.left, wnd->rect.top,
                      rectWidth(wnd->rect), rectHeight(wnd->rect));
    return TRUE;
}

HWND getOhosWindow(HWND hWnd, UINT uCmd)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    SOhosWindow *wnd = fromHwnd(hWnd);
    if (!wnd)
        return 0;
    if (uCmd == GW_OWNER)
        return wnd->owner;
    if (uCmd == GW_HWNDFIRST || uCmd == GW_HWNDLAST)
    {
        HWND sameParent = wnd->parent;
        if (uCmd == GW_HWNDFIRST)
        {
            for (HWND item : s_zOrder)
            {
                SOhosWindow *itemWnd = fromHwnd(item);
                if (itemWnd && itemWnd->parent == sameParent)
                    return item;
            }
        }
        else
        {
            for (auto it = s_zOrder.rbegin(); it != s_zOrder.rend(); ++it)
            {
                SOhosWindow *itemWnd = fromHwnd(*it);
                if (itemWnd && itemWnd->parent == sameParent)
                    return *it;
            }
        }
        return 0;
    }
    if (uCmd == GW_CHILD)
    {
        for (HWND child : s_zOrder)
        {
            SOhosWindow *childWnd = fromHwnd(child);
            if (childWnd && childWnd->parent == hWnd)
                return child;
        }
        return 0;
    }
    if (uCmd == GW_CHILDLAST)
    {
        for (auto it = s_zOrder.rbegin(); it != s_zOrder.rend(); ++it)
        {
            SOhosWindow *childWnd = fromHwnd(*it);
            if (childWnd && childWnd->parent == hWnd)
                return *it;
        }
        return 0;
    }
    if (uCmd == GW_HWNDPREV || uCmd == GW_HWNDNEXT)
    {
        auto it = std::find(s_zOrder.begin(), s_zOrder.end(), hWnd);
        if (it == s_zOrder.end())
            return 0;
        HWND sameParent = wnd->parent;
        if (uCmd == GW_HWNDPREV)
        {
            while (it != s_zOrder.begin())
            {
                --it;
                SOhosWindow *prevWnd = fromHwnd(*it);
                if (prevWnd && prevWnd->parent == sameParent)
                    return *it;
            }
        }
        else
        {
            for (++it; it != s_zOrder.end(); ++it)
            {
                SOhosWindow *nextWnd = fromHwnd(*it);
                if (nextWnd && nextWnd->parent == sameParent)
                    return *it;
            }
        }
        return 0;
    }
    return wnd->parent ? wnd->parent : wnd->owner;
}

BOOL setOhosActiveWindow(HWND hWnd)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    if (hWnd && !fromHwnd(hWnd))
        return FALSE;
    s_activeWindow = hWnd;
    return TRUE;
}

HWND getOhosActiveWindow()
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    return s_activeWindow;
}

BOOL setOhosFocusWindow(HWND hWnd)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    if (hWnd && !fromHwnd(hWnd))
        return FALSE;
    s_focusWindow = hWnd;
    return TRUE;
}

HWND getOhosFocusWindow()
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    return s_focusWindow;
}

void invalidateOhosWindow(HWND hWnd, LPCRECT)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    SOhosWindow *wnd = fromHwnd(hWnd);
    if (wnd && wnd->listener)
    {
        wnd->listener->OnNsEvent(hWnd, WM_PAINT, 0, 0);
    }
}

BOOL requestOhosWindowsRepaint()
{
    std::vector<HWND> targets;
    {
        std::lock_guard<std::recursive_mutex> lock(s_mutex);
        for (HWND hWnd : s_zOrder)
        {
            SOhosWindow *wnd = fromHwnd(hWnd);
            RECT rc;
            if (wnd && !wnd->parent && wnd->listener && visibleRectLocked(hWnd, &rc))
                targets.push_back(hWnd);
        }
    }
    for (HWND hWnd : targets)
        InvalidateRect(hWnd, nullptr, TRUE);
    return targets.empty() ? FALSE : TRUE;
}

BOOL isOhosWindowVisible(HWND hWnd)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    RECT rc;
    return visibleRectLocked(hWnd, &rc) ? TRUE : FALSE;
}

BOOL getOhosWindowRect(HWND hWnd, RECT *rc)
{
    if (!rc)
        return FALSE;
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    SOhosWindow *wnd = fromHwnd(hWnd);
    if (!wnd)
        return FALSE;
    *rc = wnd->rect;
    return TRUE;
}

HWND ohosHwndFromPoint(HWND, POINT pt)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    s_cursorPos = pt;
    for (auto it = s_zOrder.rbegin(); it != s_zOrder.rend(); ++it)
    {
        SOhosWindow *wnd = fromHwnd(*it);
        if (!wnd || wnd->transparent || !wnd->enabled)
            continue;
        RECT rc;
        if (visibleRectLocked(*it, &rc) && PtInRect(&rc, pt))
            return *it;
    }
    return 0;
}

BOOL mapOhosPointToWindow(HWND hWndFrom, HWND hWndTo, LPPOINT ppt)
{
    if (!ppt)
        return FALSE;
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    RECT fromRc = { 0, 0, 0, 0 };
    RECT toRc = { 0, 0, 0, 0 };
    if (hWndFrom && !absoluteRectLocked(hWndFrom, &fromRc))
        return FALSE;
    if (hWndTo && !absoluteRectLocked(hWndTo, &toRc))
        return FALSE;
    ppt->x += fromRc.left - toRc.left;
    ppt->y += fromRc.top - toRc.top;
    return TRUE;
}

BOOL setOhosWindowZorder(HWND hWnd, HWND hWndInsertAfter)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    if (!fromHwnd(hWnd))
        return FALSE;
    removeFromZOrderLocked(hWnd);
    if (hWndInsertAfter == HWND_BOTTOM)
    {
        s_zOrder.insert(s_zOrder.begin(), hWnd);
        return TRUE;
    }
    if (hWndInsertAfter && hWndInsertAfter != HWND_TOP && hWndInsertAfter != HWND_TOPMOST && hWndInsertAfter != HWND_NOTOPMOST)
    {
        auto it = std::find(s_zOrder.begin(), s_zOrder.end(), hWndInsertAfter);
        if (it != s_zOrder.end())
        {
            s_zOrder.insert(++it, hWnd);
            return TRUE;
        }
    }
    s_zOrder.push_back(hWnd);
    return TRUE;
}

BOOL setOhosWindowCapture(HWND hWnd)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    if (hWnd && !fromHwnd(hWnd))
        return FALSE;
    s_captureWindow = hWnd;
    return TRUE;
}

BOOL releaseOhosWindowCapture(HWND)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    s_captureWindow = 0;
    return TRUE;
}

BOOL setOhosWindowAlpha(HWND hWnd, BYTE byAlpha)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    SOhosWindow *wnd = fromHwnd(hWnd);
    if (!wnd)
        return FALSE;
    wnd->alpha = byAlpha;
    return TRUE;
}

BOOL setOhosMsgTransparent(HWND hWnd, BOOL bTransparent)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    SOhosWindow *wnd = fromHwnd(hWnd);
    if (!wnd)
        return FALSE;
    wnd->transparent = bTransparent;
    return TRUE;
}

void updateOhosWindow(HWND hWnd, const RECT &rc)
{
    invalidateOhosWindow(hWnd, &rc);
}

void commitOhosWindow(HWND hWnd, cairo_surface_t *surface, const RECT &rc)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    SOhosWindow *wnd = fromHwnd(hWnd);
    if (!wnd)
        return;
    bool alphaFixed = false;
    cairo_surface_t *snapshot = cloneWindowSurface(surface, &alphaFixed);
    if (!snapshot)
        return;
    SurfaceStats stats = getSurfaceStats(snapshot);
    if (wnd->owner && stats.maxAlpha == 0 && stats.maxColor == 0 && wnd->surface)
    {
        SOUI_WND_LOGW("ignore empty owned frame hwnd=%{public}p owner=%{public}p dirty=%{public}d,%{public}d,%{public}d,%{public}d",
                      reinterpret_cast<void *>(hWnd), reinterpret_cast<void *>(wnd->owner),
                      rc.left, rc.top, rc.right, rc.bottom);
        cairo_surface_destroy(snapshot);
        return;
    }
    if (wnd->surface)
        cairo_surface_destroy(wnd->surface);
    wnd->surface = snapshot;
    if (shouldTraceWindowLocked(wnd) && wnd->owner)
    {
        int width = cairo_image_surface_get_width(snapshot);
        int height = cairo_image_surface_get_height(snapshot);
        SOUI_WND_LOGI("commit hwnd=%{public}p owner=%{public}p surface=%{public}p surfaceSize=%{public}dx%{public}d maxAlpha=%{public}d maxColor=%{public}d alphaFixed=%{public}d dirty=%{public}d,%{public}d,%{public}d,%{public}d",
                      reinterpret_cast<void *>(hWnd), reinterpret_cast<void *>(wnd->owner), reinterpret_cast<void *>(snapshot),
                      width, height, stats.maxAlpha, stats.maxColor, alphaFixed ? 1 : 0,
                      rc.left, rc.top, rc.right, rc.bottom);
    }
    presentCompositedWindowLocked(hWnd, &rc);
}

void commitOhosWindowFromHdc(HWND hWnd, HDC hdc, const RECT &rc)
{
    if (!hdc)
        return;
    HBITMAP hBmp = (HBITMAP)GetCurrentObject(hdc, OBJ_BITMAP);
    cairo_surface_t *surface = hBmp ? (cairo_surface_t *)GetGdiObjPtr(hBmp) : nullptr;
    if (!surface)
        return;
    commitOhosWindow(hWnd, surface, rc);
}

BOOL sendOhosSysCommand(HWND, int)
{
    return FALSE;
}

BOOL setOhosWindowCursor(HWND hWnd, HCURSOR cursor)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    SOhosWindow *wnd = fromHwnd(hWnd);
    if (!wnd)
        return FALSE;
    wnd->cursor = cursor;
    WORD cursorId = cursor ? GetCursorID((HICON)cursor) : 0;
    if (cursorId != s_cursorId)
    {
        s_cursorId = cursorId;
        swinx::ohos::NotifyCursorId(cursorId);
    }
    return TRUE;
}

BOOL getOhosCursorPos(LPPOINT ppt)
{
    if (!ppt)
        return FALSE;
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    *ppt = s_cursorPos;
    return TRUE;
}

BOOL setOhosCursorPos(POINT pt)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    s_cursorPos = pt;
    return TRUE;
}

int getOhosDpi(BOOL)
{
    return 96;
}

BOOL setOhosParent(HWND hWnd, HWND hParent)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    SOhosWindow *wnd = fromHwnd(hWnd);
    if (!wnd)
        return FALSE;
    if (hParent && (!fromHwnd(hParent) || hParent == hWnd || isChildOfLocked(hParent, hWnd)))
        return FALSE;
    if (wnd->style & WS_CHILD)
    {
        wnd->parent = hParent;
        wnd->owner = 0;
    }
    else
    {
        wnd->owner = hParent;
    }
    SOUI_WND_LOGI("setParent hwnd=%{public}p parent=%{public}p owner=%{public}p arg=%{public}p style=%{public}x",
                  reinterpret_cast<void *>(hWnd), reinterpret_cast<void *>(wnd->parent), reinterpret_cast<void *>(wnd->owner),
                  reinterpret_cast<void *>(hParent), static_cast<unsigned int>(wnd->style));
    return TRUE;
}

BOOL setOhosWindowRgn(HWND, const RECT *, int)
{
    return TRUE;
}

BOOL enableOhosWindow(HWND hWnd, BOOL bEnable)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    SOhosWindow *wnd = fromHwnd(hWnd);
    if (!wnd)
        return FALSE;
    wnd->enabled = bEnable;
    return TRUE;
}

BOOL isOhosWindowMinimized(HWND)
{
    return FALSE;
}

BOOL isOhosWindowMaximized(HWND)
{
    return FALSE;
}
