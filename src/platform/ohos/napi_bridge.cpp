#include "napi_bridge.h"
#include "SOhosWindow.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <unistd.h>

#include <cairo.h>
#include <node_api.h>
#include <ace/xcomponent/native_interface_xcomponent.h>
#include <hilog/log.h>
#include <native_buffer/native_buffer.h>
#include <native_window/external_window.h>
#include <native_buffer/buffer_common.h>

namespace swinx {
namespace ohos {

static const unsigned int LOG_DOMAIN_SOUI = 0x5349;
static const char *LOG_TAG_SOUI = "SOUI_XCOMP";

#define SOUI_LOGI(fmt, ...) OH_LOG_Print(LOG_APP, LOG_INFO, ::swinx::ohos::LOG_DOMAIN_SOUI, ::swinx::ohos::LOG_TAG_SOUI, fmt, ##__VA_ARGS__)
#define SOUI_LOGW(fmt, ...) OH_LOG_Print(LOG_APP, LOG_WARN, ::swinx::ohos::LOG_DOMAIN_SOUI, ::swinx::ohos::LOG_TAG_SOUI, fmt, ##__VA_ARGS__)
#define SOUI_LOGE(fmt, ...) OH_LOG_Print(LOG_APP, LOG_ERROR, ::swinx::ohos::LOG_DOMAIN_SOUI, ::swinx::ohos::LOG_TAG_SOUI, fmt, ##__VA_ARGS__)
#if defined(SOUI_OHOS_VERBOSE_LOG)
#define SOUI_LOGV(fmt, ...) SOUI_LOGI(fmt, ##__VA_ARGS__)
#else
#define SOUI_LOGV(fmt, ...)
#endif

static std::recursive_mutex s_mutex;
static std::mutex s_presentMutex;
static std::mutex s_moveCallbackMutex;
static std::mutex s_resizeCallbackMutex;
static std::mutex s_cursorCallbackMutex;
static XComponentState s_state = {};
static napi_threadsafe_function s_moveCallback = nullptr;
static napi_threadsafe_function s_resizeCallback = nullptr;
static napi_threadsafe_function s_cursorCallback = nullptr;
static std::atomic<bool> s_imeProxyActive(false);

struct PresentConfig {
    OHNativeWindow *window;
    int width;
    int height;
    bool valid;
    bool hasFrame;
};

static PresentConfig s_presentConfig = {};

struct MoveDelta {
    int dx;
    int dy;
};

struct WindowRectData {
    int x;
    int y;
    int width;
    int height;
};

struct CursorData {
    int cursorId;
};

static int strideBytes(const BufferHandle *handle)
{
    if (!handle)
        return 0;
    int minStride = handle->width * 4;
    if (handle->stride >= minStride)
        return handle->stride;
    return handle->stride * 4;
}

static Region fullRegion(int width, int height)
{
    static Region::Rect rect;
    rect.x = 0;
    rect.y = 0;
    rect.w = width > 0 ? static_cast<uint32_t>(width) : 1;
    rect.h = height > 0 ? static_cast<uint32_t>(height) : 1;
    Region region;
    region.rects = &rect;
    region.rectNumber = 1;
    return region;
}

static bool makeDirtyRegion(const RECT *dirty, int width, int height, Region::Rect &rect, Region &region)
{
    if (!dirty)
        return false;
    int left = std::max(0, std::min(dirty->left, width));
    int top = std::max(0, std::min(dirty->top, height));
    int right = std::max(left, std::min(dirty->right, width));
    int bottom = std::max(top, std::min(dirty->bottom, height));
    if (right <= left || bottom <= top)
        return false;
    rect.x = static_cast<int32_t>(left);
    rect.y = static_cast<int32_t>(top);
    rect.w = static_cast<uint32_t>(right - left);
    rect.h = static_cast<uint32_t>(bottom - top);
    region.rects = &rect;
    region.rectNumber = 1;
    return true;
}

static XComponentState makeState(OH_NativeXComponent *component, void *window)
{
    XComponentState state = GetXComponentState();
    state.component = component;
    state.nativeWindow = window;
    state.density = state.density > 0.0f ? state.density : 1.0f;

    uint64_t width = 0;
    uint64_t height = 0;
    if (component && window && OH_NativeXComponent_GetXComponentSize(component, window, &width, &height) == 0)
    {
        state.width = static_cast<int>(width);
        state.height = static_cast<int>(height);
    }
    if (state.width <= 0)
        state.width = 1;
    if (state.height <= 0)
        state.height = 1;
    return state;
}

static bool presentPixels(const uint8_t *src, int srcWidth, int srcHeight, int srcStride, const RECT *dirty)
{
    if (!src || srcWidth <= 0 || srcHeight <= 0 || srcStride <= 0)
    {
        SOUI_LOGE("presentPixels invalid source src=%{public}p width=%{public}d height=%{public}d stride=%{public}d",
                  src, srcWidth, srcHeight, srcStride);
        return false;
    }

    XComponentState state = GetXComponentState();
    OHNativeWindow *window = static_cast<OHNativeWindow *>(state.nativeWindow);
    if (!window)
    {
        SOUI_LOGW("presentPixels skipped: nativeWindow is null");
        return false;
    }

    std::lock_guard<std::mutex> presentLock(s_presentMutex);
    int width = state.width > 0 ? state.width : srcWidth;
    int height = state.height > 0 ? state.height : srcHeight;
    if (!s_presentConfig.valid || s_presentConfig.window != window || s_presentConfig.width != width || s_presentConfig.height != height)
    {
        bool configOk = true;
        int ret = OH_NativeWindow_NativeWindowHandleOpt(window, SET_BUFFER_GEOMETRY, width, height);
        if (ret != 0)
        {
            SOUI_LOGW("SET_BUFFER_GEOMETRY failed ret=%{public}d width=%{public}d height=%{public}d", ret, width, height);
            configOk = false;
        }
        ret = OH_NativeWindow_NativeWindowHandleOpt(window, SET_FORMAT, NATIVEBUFFER_PIXEL_FMT_BGRA_8888);
        if (ret != 0)
        {
            SOUI_LOGW("SET_FORMAT BGRA failed ret=%{public}d", ret);
            configOk = false;
        }
        ret = OH_NativeWindow_NativeWindowHandleOpt(window, SET_USAGE,
                                                   NATIVEBUFFER_USAGE_CPU_READ | NATIVEBUFFER_USAGE_CPU_WRITE);
        if (ret != 0)
        {
            SOUI_LOGW("SET_USAGE CPU read/write failed ret=%{public}d", ret);
            configOk = false;
        }
        s_presentConfig.window = window;
        s_presentConfig.width = width;
        s_presentConfig.height = height;
        s_presentConfig.valid = configOk;
        s_presentConfig.hasFrame = false;
    }

    Region::Rect dirtyRect;
    Region dirtyRegion;
    bool useDirty = s_presentConfig.hasFrame && makeDirtyRegion(dirty, width, height, dirtyRect, dirtyRegion);
    Region region = useDirty ? dirtyRegion : fullRegion(width, height);
    OHNativeWindowBuffer *buffer = nullptr;
    int ret = OH_NativeWindow_LockBuffer(window, region, &buffer);
    if (ret != 0 || !buffer)
    {
        SOUI_LOGE("NativeWindowLockBuffer failed ret=%{public}d buffer=%{public}p", ret, buffer);
        return false;
    }

    BufferHandle *handle = OH_NativeWindow_GetBufferHandleFromNative(buffer);
    if (!handle || !handle->virAddr)
    {
        SOUI_LOGE("buffer handle invalid handle=%{public}p virAddr=%{public}p", handle, handle ? handle->virAddr : nullptr);
        OH_NativeWindow_UnlockAndFlushBuffer(window);
        return false;
    }

    uint8_t *dst = static_cast<uint8_t *>(handle->virAddr);
    int dstStride = strideBytes(handle);
    int copyWidth = std::min(srcWidth, handle->width);
    int copyHeight = std::min(srcHeight, handle->height);
    if (useDirty)
    {
        int left = std::min<int>(dirtyRect.x, copyWidth);
        int top = std::min<int>(dirtyRect.y, copyHeight);
        int right = std::min<int>(dirtyRect.x + dirtyRect.w, copyWidth);
        int bottom = std::min<int>(dirtyRect.y + dirtyRect.h, copyHeight);
        for (int y = top; y < bottom; ++y)
            memcpy(dst + y * dstStride + left * 4, src + y * srcStride + left * 4, (right - left) * 4);
    }
    else
    {
        for (int y = 0; y < copyHeight; ++y)
            memcpy(dst + y * dstStride, src + y * srcStride, copyWidth * 4);

        for (int y = copyHeight; y < handle->height; ++y)
            memset(dst + y * dstStride, 0xff, std::min(dstStride, handle->width * 4));
    }

    ret = OH_NativeWindow_UnlockAndFlushBuffer(window);
    if (ret != 0)
    {
        SOUI_LOGE("NativeWindowUnlockAndFlushBuffer failed ret=%{public}d", ret);
        return false;
    }
    s_presentConfig.hasFrame = true;
    SOUI_LOGV("presentPixels ok src=%{public}dx%{public}d dst=%{public}dx%{public}d stride=%{public}d",
              srcWidth, srcHeight, handle->width, handle->height, dstStride);
    return true;
}

void SetXComponentState(const XComponentState &state)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    s_state = state;
}

XComponentState GetXComponentState()
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    return s_state;
}

void SetImeProxyActive(bool active)
{
    bool old = s_imeProxyActive.exchange(active);
    if (old != active)
        SOUI_LOGI("IME proxy active=%{public}d", active ? 1 : 0);
}

bool IsImeProxyActive()
{
    return s_imeProxyActive.load();
}

void RequestFrame(HWND hWnd, const RECT *dirty)
{
    invalidateOhosWindow(hWnd, dirty);
}

bool PresentCairoSurface(cairo_surface_t *surface, const RECT *dirty)
{
    if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
        return false;
    if (cairo_surface_get_type(surface) != CAIRO_SURFACE_TYPE_IMAGE)
        return false;

    cairo_surface_flush(surface);
    return presentPixels(cairo_image_surface_get_data(surface),
                         cairo_image_surface_get_width(surface),
                         cairo_image_surface_get_height(surface),
                         cairo_image_surface_get_stride(surface),
                         dirty);
}

bool DrawDemoFrame()
{
    XComponentState state = GetXComponentState();
    if (!state.nativeWindow)
    {
        SOUI_LOGW("DrawDemoFrame skipped: nativeWindow is null");
        return false;
    }
    int width = std::max(state.width, 1);
    int height = std::max(state.height, 1);
    SOUI_LOGI("DrawDemoFrame width=%{public}d height=%{public}d touching=%{public}d", width, height, state.touching ? 1 : 0);

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *cr = cairo_create(surface);

    cairo_set_source_rgb(cr, 0.96, 0.97, 0.98);
    cairo_paint(cr);

    cairo_set_source_rgb(cr, 0.10, 0.18, 0.30);
    cairo_rectangle(cr, 0, 0, width, std::min(88, height));
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 30);
    cairo_move_to(cr, 24, 52);
    cairo_show_text(cr, "SOUI HarmonyOS");

    int cardX = 24;
    int cardY = 116;
    int cardW = std::max(1, width - 48);
    int cardH = std::min(180, std::max(80, height - 160));
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_rectangle(cr, cardX, cardY, cardW, cardH);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.74, 0.78, 0.84);
    cairo_rectangle(cr, cardX + 0.5, cardY + 0.5, cardW - 1, cardH - 1);
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, 0.13, 0.17, 0.23);
    cairo_set_font_size(cr, 22);
    cairo_move_to(cr, cardX + 22, cardY + 48);
    cairo_show_text(cr, "XComponent surface connected");

    cairo_set_source_rgb(cr, 0.26, 0.58, 0.98);
    cairo_rectangle(cr, cardX + 22, cardY + 78, 180, 44);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_font_size(cr, 18);
    cairo_move_to(cr, cardX + 50, cardY + 106);
    cairo_show_text(cr, "Native UI");

    if (state.touching)
    {
        cairo_set_source_rgba(cr, 0.26, 0.58, 0.98, 0.28);
        cairo_arc(cr, state.lastTouchX, state.lastTouchY, 28, 0, 6.28318530718);
        cairo_fill(cr);
    }

    cairo_destroy(cr);
    bool ok = PresentCairoSurface(surface, nullptr);
    cairo_surface_destroy(surface);
    return ok;
}

static void CallMoveCallback(napi_env env, napi_value jsCallback, void *, void *data)
{
    MoveDelta *delta = static_cast<MoveDelta *>(data);
    if (!delta)
        return;
    if (env && jsCallback)
    {
        napi_value global = nullptr;
        napi_value argv[2] = { nullptr, nullptr };
        napi_get_global(env, &global);
        napi_create_int32(env, delta->dx, &argv[0]);
        napi_create_int32(env, delta->dy, &argv[1]);
        napi_call_function(env, global, jsCallback, 2, argv, nullptr);
    }
    delete delta;
}

static void CallResizeCallback(napi_env env, napi_value jsCallback, void *, void *data)
{
    WindowRectData *rect = static_cast<WindowRectData *>(data);
    if (!rect)
        return;
    if (env && jsCallback)
    {
        napi_value global = nullptr;
        napi_value argv[4] = { nullptr, nullptr, nullptr, nullptr };
        napi_get_global(env, &global);
        napi_create_int32(env, rect->x, &argv[0]);
        napi_create_int32(env, rect->y, &argv[1]);
        napi_create_int32(env, rect->width, &argv[2]);
        napi_create_int32(env, rect->height, &argv[3]);
        napi_call_function(env, global, jsCallback, 4, argv, nullptr);
    }
    delete rect;
}

static void CallCursorCallback(napi_env env, napi_value jsCallback, void *, void *data)
{
    CursorData *cursor = static_cast<CursorData *>(data);
    if (!cursor)
        return;
    if (env && jsCallback)
    {
        napi_value global = nullptr;
        napi_value argv[1] = { nullptr };
        napi_get_global(env, &global);
        napi_create_int32(env, cursor->cursorId, &argv[0]);
        napi_call_function(env, global, jsCallback, 1, argv, nullptr);
    }
    delete cursor;
}

static bool SetMainWindowMoveCallback(napi_env env, napi_value callback)
{
    napi_valuetype type = napi_undefined;
    if (!callback || napi_typeof(env, callback, &type) != napi_ok || type != napi_function)
        return false;

    std::lock_guard<std::mutex> lock(s_moveCallbackMutex);
    if (s_moveCallback)
    {
        napi_release_threadsafe_function(s_moveCallback, napi_tsfn_abort);
        s_moveCallback = nullptr;
    }

    napi_value name = nullptr;
    napi_create_string_utf8(env, "soui_main_window_move", NAPI_AUTO_LENGTH, &name);
    napi_status status = napi_create_threadsafe_function(env, callback, nullptr, name, 0, 1, nullptr, nullptr, nullptr,
                                                         CallMoveCallback, &s_moveCallback);
    SOUI_LOGI("SetMainWindowMoveCallback status=%{public}d", static_cast<int>(status));
    return status == napi_ok;
}

static bool SetMainWindowResizeCallback(napi_env env, napi_value callback)
{
    napi_valuetype type = napi_undefined;
    if (!callback || napi_typeof(env, callback, &type) != napi_ok || type != napi_function)
        return false;

    std::lock_guard<std::mutex> lock(s_resizeCallbackMutex);
    if (s_resizeCallback)
    {
        napi_release_threadsafe_function(s_resizeCallback, napi_tsfn_abort);
        s_resizeCallback = nullptr;
    }

    napi_value name = nullptr;
    napi_create_string_utf8(env, "soui_main_window_resize", NAPI_AUTO_LENGTH, &name);
    napi_status status = napi_create_threadsafe_function(env, callback, nullptr, name, 0, 1, nullptr, nullptr, nullptr,
                                                         CallResizeCallback, &s_resizeCallback);
    SOUI_LOGI("SetMainWindowResizeCallback status=%{public}d", static_cast<int>(status));
    return status == napi_ok;
}

static bool SetCursorCallback(napi_env env, napi_value callback)
{
    napi_valuetype type = napi_undefined;
    if (!callback || napi_typeof(env, callback, &type) != napi_ok || type != napi_function)
        return false;

    std::lock_guard<std::mutex> lock(s_cursorCallbackMutex);
    if (s_cursorCallback)
    {
        napi_release_threadsafe_function(s_cursorCallback, napi_tsfn_abort);
        s_cursorCallback = nullptr;
    }

    napi_value name = nullptr;
    napi_create_string_utf8(env, "soui_cursor", NAPI_AUTO_LENGTH, &name);
    napi_status status = napi_create_threadsafe_function(env, callback, nullptr, name, 0, 1, nullptr, nullptr, nullptr,
                                                         CallCursorCallback, &s_cursorCallback);
    SOUI_LOGI("SetCursorCallback status=%{public}d", static_cast<int>(status));
    return status == napi_ok;
}

void NotifyMainWindowMoveDelta(int dx, int dy)
{
    if (dx == 0 && dy == 0)
        return;
    std::lock_guard<std::mutex> lock(s_moveCallbackMutex);
    if (!s_moveCallback)
        return;
    MoveDelta *delta = new MoveDelta{ dx, dy };
    napi_status status = napi_call_threadsafe_function(s_moveCallback, delta, napi_tsfn_nonblocking);
    if (status != napi_ok)
    {
        SOUI_LOGW("NotifyMainWindowMoveDelta failed status=%{public}d dx=%{public}d dy=%{public}d",
                  static_cast<int>(status), dx, dy);
        delete delta;
    }
}

void NotifyMainWindowRect(int x, int y, int width, int height)
{
    std::lock_guard<std::mutex> lock(s_resizeCallbackMutex);
    if (!s_resizeCallback)
        return;
    WindowRectData *rect = new WindowRectData{ x, y, std::max(width, 1), std::max(height, 1) };
    napi_status status = napi_call_threadsafe_function(s_resizeCallback, rect, napi_tsfn_nonblocking);
    if (status != napi_ok)
    {
        SOUI_LOGW("NotifyMainWindowRect failed status=%{public}d rect=%{public}d,%{public}d,%{public}dx%{public}d",
                  static_cast<int>(status), x, y, width, height);
        delete rect;
    }
}

void NotifyCursorId(int cursorId)
{
    std::lock_guard<std::mutex> lock(s_cursorCallbackMutex);
    if (!s_cursorCallback)
        return;
    CursorData *cursor = new CursorData{ cursorId };
    napi_status status = napi_call_threadsafe_function(s_cursorCallback, cursor, napi_tsfn_nonblocking);
    if (status != napi_ok)
    {
        SOUI_LOGW("NotifyCursorId failed status=%{public}d cursor=%{public}d", static_cast<int>(status), cursorId);
        delete cursor;
    }
}

} // namespace ohos
} // namespace swinx

namespace {

using swinx::ohos::DrawDemoFrame;
using swinx::ohos::SetXComponentState;
using swinx::ohos::XComponentState;

void OnSurfaceCreated(OH_NativeXComponent *component, void *window)
{
    XComponentState state = swinx::ohos::makeState(component, window);
    SOUI_LOGI("OnSurfaceCreated component=%{public}p window=%{public}p size=%{public}dx%{public}d",
              component, window, state.width, state.height);
    SetXComponentState(state);
    bool ok = requestOhosWindowsRepaint() ? true : DrawDemoFrame();
    SOUI_LOGI("OnSurfaceCreated DrawDemoFrame result=%{public}d", ok ? 1 : 0);
}

void OnSurfaceChanged(OH_NativeXComponent *component, void *window)
{
    XComponentState state = swinx::ohos::makeState(component, window);
    SOUI_LOGI("OnSurfaceChanged component=%{public}p window=%{public}p size=%{public}dx%{public}d",
              component, window, state.width, state.height);
    SetXComponentState(state);
    bool ok = requestOhosWindowsRepaint() ? true : DrawDemoFrame();
    SOUI_LOGI("OnSurfaceChanged DrawDemoFrame result=%{public}d", ok ? 1 : 0);
}

void OnSurfaceDestroyed(OH_NativeXComponent *component, void *window)
{
    SOUI_LOGI("OnSurfaceDestroyed component=%{public}p window=%{public}p", component, window);
    XComponentState state = swinx::ohos::GetXComponentState();
    if (state.component == component && state.nativeWindow == window)
    {
        state.nativeWindow = nullptr;
        state.width = 0;
        state.height = 0;
        state.touching = false;
        SetXComponentState(state);
    }
    {
        std::lock_guard<std::mutex> presentLock(swinx::ohos::s_presentMutex);
        if (swinx::ohos::s_presentConfig.window == window)
            swinx::ohos::s_presentConfig = {};
    }
}

void DispatchTouchEvent(OH_NativeXComponent *component, void *window)
{
    OH_NativeXComponent_TouchEvent event;
    memset(&event, 0, sizeof(event));
    if (OH_NativeXComponent_GetTouchEvent(component, window, &event) != 0)
    {
        SOUI_LOGW("GetTouchEvent failed");
        return;
    }

    XComponentState state = swinx::ohos::makeState(component, window);
    state.lastTouchX = event.x;
    state.lastTouchY = event.y;
    state.touching = event.type == OH_NATIVEXCOMPONENT_DOWN || event.type == OH_NATIVEXCOMPONENT_MOVE;
    SetXComponentState(state);
    bool ok = DrawDemoFrame();
    SOUI_LOGI("DispatchTouchEvent type=%{public}d x=%{public}.1f y=%{public}.1f draw=%{public}d",
              event.type, event.x, event.y, ok ? 1 : 0);
}

bool RegisterNativeXComponent(napi_env env, napi_value value)
{
    void *native = nullptr;
    if (napi_unwrap(env, value, &native) != napi_ok || !native)
    {
        SOUI_LOGE("RegisterNativeXComponent napi_unwrap failed value=%{public}p", value);
        return false;
    }

    static OH_NativeXComponent_Callback callback = {
        OnSurfaceCreated,
        OnSurfaceChanged,
        OnSurfaceDestroyed,
        DispatchTouchEvent,
    };

    OH_NativeXComponent *component = static_cast<OH_NativeXComponent *>(native);
    int ret = OH_NativeXComponent_RegisterCallback(component, &callback);
    SOUI_LOGI("RegisterNativeXComponent component=%{public}p ret=%{public}d", component, ret);
    return ret == 0;
}

napi_value RegisterXComponent(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value argv[1] = { nullptr };
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    bool ok = argc > 0 && argv[0] && RegisterNativeXComponent(env, argv[0]);
    SOUI_LOGI("registerXComponent argc=%{public}zu ok=%{public}d", argc, ok ? 1 : 0);
    napi_value result;
    napi_get_boolean(env, ok, &result);
    return result;
}

napi_value ShowDemo(napi_env env, napi_callback_info)
{
    bool ok = DrawDemoFrame();
    SOUI_LOGI("showDemo result=%{public}d", ok ? 1 : 0);
    napi_value result;
    napi_get_boolean(env, ok, &result);
    return result;
}

napi_value SetMainWindowMoveCallback(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value argv[1] = { nullptr };
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    bool ok = argc > 0 && argv[0] && swinx::ohos::SetMainWindowMoveCallback(env, argv[0]);
    napi_value result;
    napi_get_boolean(env, ok, &result);
    return result;
}

napi_value SetMainWindowResizeCallback(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value argv[1] = { nullptr };
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    bool ok = argc > 0 && argv[0] && swinx::ohos::SetMainWindowResizeCallback(env, argv[0]);
    napi_value result;
    napi_get_boolean(env, ok, &result);
    return result;
}

napi_value SetCursorCallback(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value argv[1] = { nullptr };
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    bool ok = argc > 0 && argv[0] && swinx::ohos::SetCursorCallback(env, argv[0]);
    napi_value result;
    napi_get_boolean(env, ok, &result);
    return result;
}

napi_value Init(napi_env env, napi_value exports)
{
    SOUI_LOGI("NAPI Init swinx");
    napi_value xcomponent;
    napi_status status = napi_get_named_property(env, exports, OH_NATIVE_XCOMPONENT_OBJ, &xcomponent);
    SOUI_LOGI("get __NATIVE_XCOMPONENT_OBJ__ status=%{public}d", static_cast<int>(status));
    if (status == napi_ok)
    {
        bool ok = RegisterNativeXComponent(env, xcomponent);
        SOUI_LOGI("auto RegisterNativeXComponent ok=%{public}d", ok ? 1 : 0);
    }

    napi_property_descriptor desc[] = {
        { "registerXComponent", nullptr, RegisterXComponent, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "showDemo", nullptr, ShowDemo, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "setMainWindowMoveCallback", nullptr, SetMainWindowMoveCallback, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "setMainWindowResizeCallback", nullptr, SetMainWindowResizeCallback, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "setCursorCallback", nullptr, SetCursorCallback, nullptr, nullptr, nullptr, napi_default, nullptr },
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}

extern "C" {
static napi_module g_swinxModule = {
    NAPI_MODULE_VERSION,
    0,
    __FILE__,
    Init,
    "swinx",
    nullptr,
    { 0 },
};

static napi_module g_libswinxModule = {
    NAPI_MODULE_VERSION,
    0,
    __FILE__,
    Init,
    "libswinx",
    nullptr,
    { 0 },
};

__attribute__((constructor)) static void RegisterSwinxModules()
{
    napi_module_register(&g_swinxModule);
    napi_module_register(&g_libswinxModule);
}
}

} // namespace
