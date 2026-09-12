/*
 * swinx MSAA (IAccessible) <-> macOS NSAccessibility 桥接（胶水层）
 *
 * 见 SNsAccessibility.h。要点：
 *  - SwinxAccElement 是 NSAccessibilityElement 的子类，但只是 (hwnd, child 链)
 *    的"身份壳"，不持有任何 COM 引用；
 *  - 每次属性/动作访问都经 SwinxAccResolvePath（swinx 内部助手，
 *    src/SwinxAccGlue.h）重新向窗口发 WM_GETOBJECT 查询（SHostWnd 按
 *    SOUI_ENABLE_ACC 决定返回 NULL 还是 IAccessible），拿到接口现查现用现放，
 *    swinx 不持有 IAccessible；
 *  - NotifyWinEvent 经标准 SetWinEventHook（winuser.h）转发到这里，映射为
 *    NSAccessibility 通知；事件可能来自任意线程，统一 dispatch 到主队列。
 */
#import "SNsAccessibility.h"
#import "SNsCoord.h"

#include <vector>

#include "log.h"
#define kLogTag "SwinxAcc"

/* SwinxAccResolvePath：swinx 内部助手（src/SwinxAccGlue.h，非公共 API 面），
 * 按需向窗口发 WM_GETOBJECT 解析 (hwnd, child 链) 处的 IAccessible。 */
#include "SwinxAccGlue.h"

/* 下钻内核与壳定位（实现见文件后部"命中测试"节；SwinxAccElement 的
 * -accessibilityHitTest: 需要在 @implementation 内提前使用） */
static NSArray *SwinxAccHitTestChain(HWND hwnd, POINT pt, NSArray *startChain);
static id SwinxAccShellForChain(HWND hwnd, NSArray *chain);

#pragma mark - 小工具

/* swinx 的 WCHAR 在 macOS 是 wchar_t（4 字节），BSTR 载荷即 UTF-32 */
static NSString *SwinxAccStringFromBstr(BSTR s)
{
    if (!s)
        return nil;
    NSUInteger n = (NSUInteger)SysStringLen(s);
    if (n == 0)
        return @"";
    return [[NSString alloc] initWithBytes:s
                                    length:n * sizeof(wchar_t)
                                  encoding:NSUTF32LittleEndianStringEncoding];
}

/* 构造用于寻址子元素的 VARIANT（MSAA 里子元素就是 VT_I4 的 child id） */
static VARIANT SwinxAccChildVariant(LONG childId)
{
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_I4;
    v.lVal = childId;
    return v;
}

static BOOL SwinxAccVariantLong(const VARIANT *v, LONG *out)
{
    if (!v || !out)
        return NO;
    if (v->vt == VT_I4)
    {
        *out = v->lVal;
        return YES;
    }
    if ((v->vt & VT_TYPEMASK) == VT_I4 && (v->vt & VT_BYREF) && v->plVal)
    {
        *out = *v->plVal;
        return YES;
    }
    return NO;
}

static BOOL SwinxAccGetState(IAccessible *acc, LONG childId, LONG *outState)
{
    if (!acc || !outState)
        return NO;
    *outState = 0;
    VARIANT child = SwinxAccChildVariant(childId);
    VARIANT v;
    VariantInit(&v);
    HRESULT hr = acc->get_accState(child, &v);
    VariantClear(&child);
    if (hr != S_OK)
        return NO;
    LONG st = 0;
    BOOL ok = SwinxAccVariantLong(&v, &st);
    VariantClear(&v);
    if (ok)
        *outState = st;
    return ok;
}

#pragma mark - 角色映射

static NSString *SwinxAccRoleFromMsaA(DWORD role)
{
    switch (role)
    {
    case ROLE_SYSTEM_PUSHBUTTON:
        return NSAccessibilityButtonRole;
    case ROLE_SYSTEM_CHECKBUTTON:
        return NSAccessibilityCheckBoxRole;
    case ROLE_SYSTEM_RADIOBUTTON:
        return NSAccessibilityRadioButtonRole;
    case ROLE_SYSTEM_STATICTEXT:
        return NSAccessibilityStaticTextRole;
    case ROLE_SYSTEM_TEXT:
        return NSAccessibilityTextFieldRole;
    case ROLE_SYSTEM_LINK:
        return NSAccessibilityLinkRole;
    case ROLE_SYSTEM_WINDOW:
    case ROLE_SYSTEM_DIALOG:
    case ROLE_SYSTEM_TITLEBAR:
        return NSAccessibilityWindowRole;
    case ROLE_SYSTEM_CLIENT:
    case ROLE_SYSTEM_PANE:
    case ROLE_SYSTEM_GROUPING:
    case ROLE_SYSTEM_SEPARATOR:
        return NSAccessibilityGroupRole;
    case ROLE_SYSTEM_LIST:
        return NSAccessibilityListRole;
    case ROLE_SYSTEM_MENUITEM:
        return NSAccessibilityMenuItemRole;
    case ROLE_SYSTEM_MENUBAR:
        return NSAccessibilityMenuBarRole;
    case ROLE_SYSTEM_TOOLBAR:
        return NSAccessibilityToolbarRole;
    case ROLE_SYSTEM_COMBOBOX:
    case ROLE_SYSTEM_DROPLIST:
        return NSAccessibilityPopUpButtonRole;
    case ROLE_SYSTEM_SLIDER:
        return NSAccessibilitySliderRole;
    case ROLE_SYSTEM_PROGRESSBAR:
        return NSAccessibilityProgressIndicatorRole;
    case ROLE_SYSTEM_SCROLLBAR:
        return NSAccessibilityScrollBarRole;
    case ROLE_SYSTEM_GRAPHIC:
        return NSAccessibilityImageRole;
    default:
        return NSAccessibilityUnknownRole;
    }
}

#pragma mark - SwinxAccElement（身份壳，按需解析）

@implementation SwinxAccElement {
    HWND _hwnd;
    NSArray<NSNumber *> *_chain;
    __weak id _parent;
    NSArray *_children; // 子元素"壳"缓存（不含任何 COM 引用）
}

- (instancetype)initWithHwnd:(HWND)hwnd
                       chain:(NSArray<NSNumber *> *)chain
                      parent:(id)parent
{
    if ((self = [super init]))
    {
        _hwnd = hwnd;
        _chain = [chain copy] ?: @[];
        _parent = parent;
    }
    return self;
}

- (HWND)swinxHwnd
{
    return _hwnd;
}

- (NSArray<NSNumber *> *)swinxChain
{
    return _chain;
}

- (void)swinxInvalidateChildren
{
    _children = nil;
}

/* 按需解析：向窗口重新发 WM_GETOBJECT（SHostWnd 决定返回 NULL 还是
 * IAccessible），沿 child 链下钻到本元素。YES 时调用方负责 Release(*pAcc)。 */
- (BOOL)swinxResolve:(IAccessible **)pAcc childId:(LONG *)pChildId
{
    if (!pAcc || !pChildId)
        return NO;
    *pAcc = NULL;
    *pChildId = CHILDID_SELF;

    std::vector<LONG> chain;
    chain.reserve(_chain.count);
    for (NSNumber *n in _chain)
        chain.push_back((LONG)[n longValue]);

    return SwinxAccResolvePath(_hwnd, chain.empty() ? NULL : &chain[0], (LONG)chain.size(), pAcc,
                               pChildId)
        == S_OK;
}

/* 用查询到的 (acc, childId) 执行一次访问，自动放掉引用 */
template <typename T>
static T SwinxAccQuery(SwinxAccElement *self, T (^block)(IAccessible *acc, LONG childId),
                       T fallback)
{
    IAccessible *acc = NULL;
    LONG childId = CHILDID_SELF;
    if (![self swinxResolve:&acc childId:&childId] || !acc)
        return fallback;
    T ret = block(acc, childId);
    acc->Release();
    return ret;
}

#pragma mark NSAccessibility（全部属性实时查询）

- (BOOL)isAccessibilityElement
{
    return YES;
}

- (id)accessibilityParent
{
    if (!_parent)
    {
        /* 祖先链出现孤儿壳（根壳尚未挂视图/窗口已销毁后 VoiceOver 仍持有
         * 旧壳）。AppKit 解析元素时要沿 accessibilityParent 上溯找原生锚点，
         * 中途遇 nil 会解析失败并无限重试（AX 查询洪水钉死主线程）——这里
         * 以 NSApp 兜底保证链必达终点。限流日志用于诊断孤儿壳来源。 */
        static CFTimeInterval s_lastLog = 0;
        CFTimeInterval now = [NSDate timeIntervalSinceReferenceDate];
        if (now - s_lastLog > 2.0)
        {
            s_lastLog = now;
            SLOG_STMW() << "accessibilityParent nil fallback to NSApp,chain="
                        << (long)_chain.count;
        }
        return NSApp;
    }
    return _parent;
}

- (void)setAccessibilityParent:(id)parent
{
    _parent = parent;
}

- (NSString *)accessibilityRole
{
    return SwinxAccQuery(self,
                         ^NSString *(IAccessible *acc, LONG childId) {
                             VARIANT child = SwinxAccChildVariant(childId);
                             VARIANT v;
                             VariantInit(&v);
                             HRESULT hr = acc->get_accRole(child, &v);
                             VariantClear(&child);
                             if (hr != S_OK)
                                 return NSAccessibilityUnknownRole;
                             LONG role = 0;
                             BOOL ok = SwinxAccVariantLong(&v, &role);
                             VariantClear(&v);
                             return ok ? SwinxAccRoleFromMsaA((DWORD)role)
                                       : NSAccessibilityUnknownRole;
                         },
                         NSAccessibilityUnknownRole);
}

- (NSString *)accessibilityRoleDescription
{
    return NSAccessibilityRoleDescription([self accessibilityRole], nil);
}

- (NSString *)accessibilityTitle
{
    return SwinxAccQuery(self,
                         ^NSString *(IAccessible *acc, LONG childId) {
                             VARIANT child = SwinxAccChildVariant(childId);
                             BSTR name = NULL;
                             HRESULT hr = acc->get_accName(child, &name);
                             VariantClear(&child);
                             if (hr != S_OK || !name)
                                 return (NSString *)nil;
                             NSString *s = SwinxAccStringFromBstr(name);
                             SysFreeString(name);
                             return s;
                         },
                         (NSString *)nil);
}

- (id)accessibilityValue
{
    return SwinxAccQuery(self,
                         ^id(IAccessible *acc, LONG childId) {
                             VARIANT child = SwinxAccChildVariant(childId);
                             BSTR val = NULL;
                             HRESULT hr = acc->get_accValue(child, &val);
                             VariantClear(&child);
                             if (hr != S_OK || !val)
                                 return (id)nil;
                             NSString *s = SwinxAccStringFromBstr(val);
                             SysFreeString(val);
                             return s;
                         },
                         (id)nil);
}

- (NSString *)accessibilityHelp
{
    return SwinxAccQuery(self,
                         ^NSString *(IAccessible *acc, LONG childId) {
                             VARIANT child = SwinxAccChildVariant(childId);
                             BSTR desc = NULL;
                             HRESULT hr = acc->get_accDescription(child, &desc);
                             VariantClear(&child);
                             if (hr != S_OK || !desc)
                                 return (NSString *)nil;
                             NSString *s = SwinxAccStringFromBstr(desc);
                             SysFreeString(desc);
                             return s;
                         },
                         (NSString *)nil);
}

/* accLocation 给出 swinx 全局像素矩形（主屏左上原点、y 向下）。
 * 借 SNsCoord 的换算转到 Cocoa 屏幕坐标（point、y 向上、原点在左下）。 */
- (NSRect)accessibilityFrame
{
    return SwinxAccQuery(self,
                         ^NSRect(IAccessible *acc, LONG childId) {
                             long x = 0, y = 0, w = 0, h = 0;
                             VARIANT child = SwinxAccChildVariant(childId);
                             HRESULT hr = acc->accLocation(&x, &y, &w, &h, child);
                             VariantClear(&child);
                             if (hr != S_OK || w <= 0 || h <= 0)
                                 return NSZeroRect;
                             NSPoint tl = swinxNsCocoaPointFromWin((POINT){(LONG)x, (LONG)y});
                             NSPoint br =
                                 swinxNsCocoaPointFromWin((POINT){(LONG)(x + w), (LONG)(y + h)});
                             return NSMakeRect(tl.x, br.y, br.x - tl.x, tl.y - br.y);
                         },
                         NSZeroRect);
}

- (NSRect)accessibilityFrameInParentSpace
{
    NSRect screen = [self accessibilityFrame];
    if (NSIsEmptyRect(screen))
        return screen;

    id parent = _parent;
    if ([parent isKindOfClass:[NSView class]])
    {
        NSView *view = (NSView *)parent;
        NSRect inWindow = [view.window convertRectFromScreen:screen];
        return [view convertRect:inWindow fromView:nil];
    }
    if ([parent respondsToSelector:@selector(accessibilityFrame)])
    {
        NSRect pr = [parent accessibilityFrame];
        return NSMakeRect(screen.origin.x - pr.origin.x, screen.origin.y - pr.origin.y,
                          screen.size.width, screen.size.height);
    }
    return screen;
}

/* 实时枚举子元素：解析当前对象（必须是完整对象，简单元素没有子节点），按
 * child id 1..count 构建子壳。与缓存的旧壳按链匹配复用，维持 AX 对象身份
 * 稳定；壳不持 COM 引用，重建零成本。 */
- (NSArray *)accessibilityChildren
{
    NSMutableArray *kids = [NSMutableArray array];

    IAccessible *acc = NULL;
    LONG childId = CHILDID_SELF;
    if ([self swinxResolve:&acc childId:&childId] && acc)
    {
        if (childId == CHILDID_SELF)
        {
            long count = 0;
            if (acc->get_accChildCount(&count) == S_OK && count > 0)
            {
                for (long i = 1; i <= count; i++)
                {
                    NSMutableArray<NSNumber *> *childChain = [_chain mutableCopy];
                    [childChain addObject:@(i)];
                    [kids addObject:[self swinxChildShellWithChain:childChain]];
                }
            }
        }
        acc->Release();
    }

    _children = kids;
    return _children;
}

/* 在缓存里找链相同的旧壳复用，找不到就建新壳 */
- (SwinxAccElement *)swinxChildShellWithChain:(NSArray<NSNumber *> *)childChain
{
    for (SwinxAccElement *old in _children)
    {
        if ([old isKindOfClass:[SwinxAccElement class]] && [old swinxChain].count == childChain.count)
        {
            BOOL same = YES;
            for (NSUInteger i = 0; i < childChain.count; i++)
            {
                if ([old swinxChain][i].unsignedLongValue != childChain[i].unsignedLongValue)
                {
                    same = NO;
                    break;
                }
            }
            if (same)
                return old;
        }
    }
    return [[SwinxAccElement alloc] initWithHwnd:_hwnd chain:childChain parent:self];
}

- (BOOL)accessibilityEnabled
{
    return SwinxAccQuery(self,
                         ^BOOL(IAccessible *acc, LONG childId) {
                             LONG state = 0;
                             if (!SwinxAccGetState(acc, childId, &state))
                                 return YES; /* 查不到状态时按可用处理 */
                             return (state & STATE_SYSTEM_UNAVAILABLE) == 0;
                         },
                         YES);
}

- (BOOL)accessibilityFocused
{
    return SwinxAccQuery(self,
                         ^BOOL(IAccessible *acc, LONG childId) {
                             LONG state = 0;
                             if (!SwinxAccGetState(acc, childId, &state))
                                 return NO;
                             return (state & STATE_SYSTEM_FOCUSED) != 0;
                         },
                         NO);
}

- (BOOL)accessibilityHidden
{
    return SwinxAccQuery(self,
                         ^BOOL(IAccessible *acc, LONG childId) {
                             LONG state = 0;
                             if (!SwinxAccGetState(acc, childId, &state))
                                 return NO;
                             return (state & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN)) != 0;
                         },
                         NO);
}

- (BOOL)accessibilityPerformPress
{
    return SwinxAccQuery(self,
                         ^BOOL(IAccessible *acc, LONG childId) {
                             VARIANT child = SwinxAccChildVariant(childId);
                             HRESULT hr = acc->accDoDefaultAction(child);
                             VariantClear(&child);
                             return hr == S_OK;
                         },
                         NO);
}

- (void)accessibilityPerformAction:(NSAccessibilityActionName)action
{
    /* NSAccessibility 协议声明该方法返回 void（是否处理成功不向系统回报） */
    if ([action isEqualToString:NSAccessibilityPressAction])
        [self accessibilityPerformPress];
}

/* 同线程重入防护计数器：AX 命中查询处理中（SendMessage WM_GETOBJECT 重入
 * 窗口过程等）若再次触发 AX 命中查询，直接浅层应答并记日志，防递归放大 */
static int s_accHitTestDepth = 0;

/* 系统命中查询可能逐级落到任一壳元素上（AppKit 的一般实现按 frame 预筛选后
 * 才调用 accessibilityHitTest:，"系统已确保 point 落在本元素内"）。从本壳的
 * 链出发继续在 swinx 的 IAccessible 树上下钻，返回更深壳；命中自身则返回 self。 */
- (id)accessibilityHitTest:(NSPoint)point
{
    if (s_accHitTestDepth > 0)
    {
        SLOG_STMW() << "shell hit test REENTRANT,depth=" << s_accHitTestDepth;
        return self;
    }
    POINT pt;
    swinxNsWinPointFromCocoa(point, &pt);
    s_accHitTestDepth++;
    NSArray *chain = SwinxAccHitTestChain(_hwnd, pt, _chain);
    s_accHitTestDepth--;
    if (chain.count <= _chain.count)
        return self;
    return SwinxAccShellForChain(_hwnd, chain);
}

@end

#pragma mark - 身份壳缓存 / 初始化

static NSMutableDictionary *SwinxAccRoots(void)
{
    static NSMutableDictionary *dict = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        dict = [[NSMutableDictionary alloc] init];
    });
    return dict;
}

static NSNumber *SwinxAccRootKey(HWND hwnd)
{
    return [NSNumber numberWithUnsignedLongLong:(unsigned long long)(uintptr_t)hwnd];
}

/* 只查缓存、不创建——事件处理用，避免为没有无障碍对象的窗口凭空造元素 */
static SwinxAccElement *SwinxAccCachedRoot(HWND hwnd)
{
    if (!hwnd)
        return nil;
    id obj = SwinxAccRoots()[SwinxAccRootKey(hwnd)];
    return [obj isKindOfClass:[SwinxAccElement class]] ? obj : nil;
}

/* 事件钩子定义在文件末尾，这里提前声明供 SwinxNsAccInit 注册 */
static void CALLBACK SwinxAccEventHook(HWINEVENTHOOK hHook, DWORD event, HWND hwnd, LONG idObject,
                                       LONG idChild, DWORD idEventThread, DWORD dwmsEventTime);

BOOL SwinxNsAccInit(void)
{
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        /* 标准 user32 API：SetWinEventHook（winuser.h），EVENT_MIN..EVENT_MAX 全量 */
        SetWinEventHook(EVENT_MIN, EVENT_MAX, NULL, &SwinxAccEventHook, 0, 0, WINEVENT_OUTOFCONTEXT);
    });
    return YES;
}

SwinxAccElement *SwinxNsAccRootElement(HWND hwnd)
{
    if (!hwnd)
        return nil;
    SwinxNsAccInit();

    NSMutableDictionary *roots = SwinxAccRoots();
    NSNumber *key = SwinxAccRootKey(hwnd);
    id cached = roots[key];
    if ([cached isKindOfClass:[SwinxAccElement class]])
        return cached;

    /* 先探测窗口是否真的提供可访问对象（acc 关闭时 SHostWnd 对 WM_GETOBJECT
     * 返回 0）：失败就不建壳、不缓存，下次再问。 */
    IAccessible *probe = NULL;
    LONG probeChild = CHILDID_SELF;
    if (SwinxAccResolvePath(hwnd, NULL, 0, &probe, &probeChild) != S_OK)
        return nil;
    probe->Release();

    SwinxAccElement *el = [[SwinxAccElement alloc] initWithHwnd:hwnd chain:@[] parent:nil];
    roots[key] = el;
    return el;
}

NSArray *SwinxNsAccChildrenForHwnd(HWND hwnd, id parent)
{
    SwinxNsAccAttachRootParent(hwnd, parent);
    SwinxAccElement *root = SwinxNsAccRootElement(hwnd);
    if (!root)
        return @[];
    return @[ root ];
}

void SwinxNsAccAttachRootParent(HWND hwnd, id parent)
{
    if (!hwnd || !parent)
        return;
    /* root 可能尚不存在：先建壳（内部含 WM_GETOBJECT 探测）。setter 幂等，
     * 重复赋同一视图零成本。 */
    SwinxAccElement *root = SwinxNsAccRootElement(hwnd);
    if (root)
        [root setAccessibilityParent:parent];
}

void SwinxNsAccInvalidate(HWND hwnd)
{
    if (!hwnd)
        return;
    [SwinxAccRoots() removeObjectForKey:SwinxAccRootKey(hwnd)];
}

#pragma mark - 命中测试（VoiceOver 鼠标跟随）

/* 比较 IAccessible 是否同一底层对象（按 IUnknown 身份，非指针值） */
static BOOL SwinxAccSameObject(IAccessible *a, IAccessible *b)
{
    if (!a || !b)
        return NO;
    if (a == b)
        return YES;
    IUnknown *ua = NULL, *ub = NULL;
    a->QueryInterface(IID_IUnknown, (void **)&ua);
    b->QueryInterface(IID_IUnknown, (void **)&ub);
    BOOL same = ua && ub && ua == ub;
    if (ua)
        ua->Release();
    if (ub)
        ub->Release();
    return same;
}

/* 命中下钻内核：从 startChain（空 = 窗口根）出发，在 hwnd 的 IAccessible 树上
 * 逐级 accHitTest，返回最深元素的 child id 链（可能就是 startChain）。
 * SOUI 的 accHitTest 是单层的（返回直接子 id 或 SELF），真实子窗口是完整对象
 * （get_accChild 给出 IDispatch），可以继续对其 accHitTest；虚拟（简单元素）
 * 子没有子节点，即为终点。 */
static NSArray *SwinxAccHitTestChain(HWND hwnd, POINT pt, NSArray *startChain)
{
    NSMutableArray<NSNumber *> *chain = [startChain mutableCopy] ?: [NSMutableArray array];
    for (int depth = 0; depth < 64; depth++)
    {
        std::vector<LONG> path;
        path.reserve(chain.count);
        for (NSNumber *n in chain)
            path.push_back((LONG)[n longValue]);

        IAccessible *acc = NULL;
        LONG childId = CHILDID_SELF;
        HRESULT hrr =
            SwinxAccResolvePath(hwnd, path.empty() ? NULL : &path[0], (LONG)path.size(), &acc,
                                &childId);
        if (hrr != S_OK)
            break;
        if (!acc)
            break;
        if (childId != CHILDID_SELF)
        { /* 简单元素：链已到终点 */
            acc->Release();
            break;
        }

        VARIANT v;
        VariantInit(&v);
        HRESULT hr = acc->accHitTest((LONG)pt.x, (LONG)pt.y, &v);
        LONG childIndex = 0;
        if (hr == S_OK && v.vt == VT_I4)
        {
            childIndex = v.lVal; /* SELF(0) 或该层直接子的 1..count 编号 */
        }
        else if (hr == S_OK && (v.vt & VT_TYPEMASK) == VT_DISPATCH && v.pdispVal)
        { /* MSAA 允许返回完整子对象：按身份在直接子中定位编号 */
            IAccessible *hit = NULL;
            if (v.pdispVal->QueryInterface(IID_IAccessible, (void **)&hit) == S_OK)
            {
                long count = 0;
                if (acc->get_accChildCount(&count) == S_OK)
                {
                    for (LONG i = 1; i <= count && childIndex == 0; i++)
                    {
                        VARIANT cv;
                        VariantInit(&cv);
                        cv.vt = VT_I4;
                        cv.lVal = i;
                        IDispatch *d = NULL;
                        if (acc->get_accChild(cv, &d) == S_OK && d)
                        {
                            IAccessible *c = NULL;
                            if (d->QueryInterface(IID_IAccessible, (void **)&c) == S_OK)
                            {
                                if (SwinxAccSameObject(c, hit))
                                    childIndex = i;
                                c->Release();
                            }
                            d->Release();
                        }
                        VariantClear(&cv);
                    }
                }
                hit->Release();
            }
        }
        VariantClear(&v);
        acc->Release();

        if (childIndex <= 0)
            break; /* 命中当前层自身或解析失败：链到终点 */
        [chain addObject:@(childIndex)];
    }
    return chain;
}

/* 由根壳沿链逐级取/建子壳：走 accessibilityChildren 复用缓存，维持 parent 链
 * 与对象身份稳定；id 超出枚举范围时按链直接建壳兜底。 */
static id SwinxAccShellForChain(HWND hwnd, NSArray *chain)
{
    SwinxAccElement *root = SwinxNsAccRootElement(hwnd);
    if (!root)
        return nil;
    id cur = root;
    for (NSUInteger i = 0; i < chain.count; i++)
    {
        LONG idv = (LONG)[chain[i] longValue];
        NSArray *kids = [cur accessibilityChildren];
        if (idv >= 1 && idv <= (LONG)kids.count)
        {
            cur = kids[idv - 1];
        }
        else
        { /* 虚拟（简单元素）子不在枚举范围：按链直接建壳（链的最后一级） */
            cur = [[SwinxAccElement alloc] initWithHwnd:hwnd
                                                  chain:[chain subarrayWithRange:NSMakeRange(0, i + 1)]
                                                 parent:cur];
            break;
        }
    }
    return cur;
}

id SwinxNsAccHitTest(HWND hwnd, NSPoint screenPoint)
{
    if (s_accHitTestDepth > 0)
    {
        SLOG_STMW() << "root hit test REENTRANT,depth=" << s_accHitTestDepth;
        return SwinxNsAccRootElement(hwnd);
    }
    SwinxAccElement *root = SwinxNsAccRootElement(hwnd);
    if (!root)
        return nil;

    POINT pt;
    swinxNsWinPointFromCocoa(screenPoint, &pt);
    s_accHitTestDepth++;
    NSArray *chain = SwinxAccHitTestChain(hwnd, pt, @[]);
    s_accHitTestDepth--;
    id ret = (chain.count == 0) ? (id)root : SwinxAccShellForChain(hwnd, chain);
    /* AX 契约：elementAtPosition 返回的元素应包含查询点。若叶子 frame 与查询点
     * 不一致（换算误差/异常数据），旁白光标会朝错误目标游走不收敛；此处回退
     * 到根（窗口 frame 必含查询点），保证收敛。容差 2pt 吸收取整误差。 */
    if (ret && ret != (id)root)
    {
        NSRect f = NSInsetRect([ret accessibilityFrame], -2, -2);
        if (!NSPointInRect(screenPoint, f))
        {
            SLOG_STMW() << "hit test frame mismatch,pt=" << (long)screenPoint.x << ","
                        << (long)screenPoint.y << ",frame=" << (long)f.origin.x << ","
                        << (long)f.origin.y << "," << (long)f.size.width << ","
                        << (long)f.size.height;
            ret = root;
        }
    }
    return ret;
}

#pragma mark - 事件桥接

/* MSAA 事件 -> NSAccessibility 通知；invalidate 表示需要丢弃子壳缓存 */
static NSString *SwinxAccNotificationForEvent(DWORD event, BOOL *invalidate)
{
    *invalidate = NO;
    switch (event)
    {
    case EVENT_OBJECT_FOCUS:
        return NSAccessibilityFocusedUIElementChangedNotification;
    case EVENT_OBJECT_NAMECHANGE:
        return NSAccessibilityTitleChangedNotification;
    case EVENT_OBJECT_VALUECHANGE:
    case EVENT_OBJECT_STATECHANGE:
        return NSAccessibilityValueChangedNotification;
    case EVENT_OBJECT_SELECTION:
    case EVENT_OBJECT_SELECTIONADD:
    case EVENT_OBJECT_SELECTIONREMOVE:
        return NSAccessibilitySelectedChildrenChangedNotification;
    case EVENT_OBJECT_LOCATIONCHANGE:
        return NSAccessibilityMovedNotification;
    case EVENT_OBJECT_CREATE:
        *invalidate = YES;
        return NSAccessibilityCreatedNotification;
    case EVENT_OBJECT_DESTROY:
        *invalidate = YES;
        return NSAccessibilityUIElementDestroyedNotification;
    case EVENT_OBJECT_SHOW:
    case EVENT_OBJECT_HIDE:
        *invalidate = YES;
        return nil;
    default:
        return nil;
    }
}

static void CALLBACK SwinxAccEventHook(HWINEVENTHOOK hHook, DWORD event, HWND hwnd, LONG idObject,
                                       LONG idChild, DWORD idEventThread, DWORD dwmsEventTime)
{
    (void)hHook;
    (void)idEventThread;
    (void)dwmsEventTime;
    (void)idObject;
    (void)idChild;
    if (!hwnd)
        return;

    BOOL invalidate = NO;
    NSString *notification = SwinxAccNotificationForEvent(event, &invalidate);

    /* 通知节流：只丢弃 NSAccessibility 通知的投递（同一窗口最短 50ms 间隔），
     * 壳缓存失效不受影响。SOUI 的 STATECHANGE/LOCATIONCHANGE 在鼠标移动等
     * 场景会连发，VoiceOver 对每个通知都会重新查询相关子树，事件→通知→查询
     * 可能互相放大成查询洪水；节流不影响属性的实时查询语义。 */
    if (notification)
    {
        static NSMutableDictionary *s_lastPost = nil;
        static dispatch_once_t onceTok;
        dispatch_once(&onceTok, ^{ s_lastPost = [[NSMutableDictionary alloc] init]; });
        NSNumber *key = [NSNumber numberWithUnsignedLongLong:(unsigned long long)(uintptr_t)hwnd];
        @synchronized(s_lastPost)
        {
            CFTimeInterval now = [NSDate timeIntervalSinceReferenceDate];
            CFTimeInterval last = [s_lastPost[key] doubleValue];
            if (now - last < 0.05)
            {
                notification = nil;
            }
            else
            {
                s_lastPost[key] = @(now);
            }
        }
    }

    /* 事件可能来自任意线程，NSAccessibility 通知必须在主线程投递 */
    dispatch_async(dispatch_get_main_queue(), ^{
        @autoreleasepool
        {
            SwinxAccElement *root = SwinxAccCachedRoot(hwnd);
            if (!root)
                return; /* 还没人查询过这棵树，无需通知 */

            if (invalidate)
            {
                [root swinxInvalidateChildren]; /* 只丢壳，无 COM 泄漏 */
                if (!notification)
                    return;
            }

            NSAccessibilityPostNotification(root, notification);
        }
    });
}
