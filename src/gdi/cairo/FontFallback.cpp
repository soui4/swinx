#include <windows.h>
#include <cairo.h>
#include <cairo-ft.h>
#include <fontconfig/fontconfig.h>
#include <assert.h>
#include <ctype.h>
#include <atomic>
#include <mutex>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "log.h"
#define kLogTag "FontFallback"
#include "uniconv.h"
#include "FontFallback.h"

// cairo user data key for the per-context fallback data
static const cairo_user_data_key_t kFontFallbackKey = {};

//////////////////////////////////////////////////////////////////////////////
// helpers

bool SwinXIsCjkFontAlias(const char *faceName)
{
    if (!faceName || !faceName[0])
        return true; // default font: assume mixed script UI, want CJK fallback

    // lowercase the name once
    swinx_stl::string name;
    name.reserve(strlen(faceName));
    for (const char *p = faceName; *p; ++p)
        name += (char)tolower((unsigned char)*p);

    // Windows fonts and generic aliases that do not exist on Linux/OHOS
    static const char *aliases[] = {
        "simsun", "nsimsun", "simhei", "kaiti", "fangsong", "youyuan",
        "microsoft yahei", "msyh", "microsoft jhenghei", "msjh",
        "dengxian", "pmingliu", "mingliu", "simsun-extb",
        "arial", "sans", "sans-serif", "serif", "system", "fixedsys", "terminal",
    };
    for (const char *alias : aliases)
    {
        if (name == alias)
            return true;
    }

    // any non-ASCII byte (Chinese face names, e.g. "宋体") => likely CJK
    for (const unsigned char *p = (const unsigned char *)faceName; *p; ++p)
    {
        if ((*p) & 0x80)
            return true;
    }
    return false;
}

FcPattern *BuildFontMatchPattern(const LOGFONTA *lf, bool zhHint,
                                 const char *const *extraFamilies, int nExtraFamilies,
                                 bool zhCharsetHint)
{
    const char *faceName = lf->lfFaceName[0] ? lf->lfFaceName : "sans-serif";
    FcPattern *pat = FcPatternCreate();
    if (!pat)
        return NULL;

    if (zhHint)
    {
        // platform preferred families first (when provided)
        for (int i = 0; i < nExtraFamilies && extraFamilies; i++)
            FcPatternAddString(pat, FC_FAMILY, (const FcChar8 *)extraFamilies[i]);
        if (!extraFamilies || nExtraFamilies == 0)
            FcPatternAddString(pat, FC_FAMILY, (const FcChar8 *)faceName);
        FcPatternAddString(pat, FC_LANG, (const FcChar8 *)"zh-cn");
        if (zhCharsetHint)
        {
            FcCharSet *charset = FcCharSetCreate();
            if (charset)
            {
                FcCharSetAddChar(charset, 0x4E2D); // U+4E2D 中
                FcPatternAddCharSet(pat, FC_CHARSET, charset);
                FcCharSetDestroy(charset);
            }
        }
    }
    else
    {
        FcPatternAddString(pat, FC_FAMILY, (const FcChar8 *)faceName);
    }

    FcPatternAddInteger(pat, FC_WEIGHT, lf->lfWeight > 400 ? FC_WEIGHT_BOLD : FC_WEIGHT_NORMAL);
    FcPatternAddInteger(pat, FC_SLANT, lf->lfItalic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    return pat;
}

//////////////////////////////////////////////////////////////////////////////
// system font list: enumerated once by a background thread

// decode one utf8 codepoint; returns the codepoint and its byte length in
// *len. byte length comes from swinx::UTF8CharLength; a sequence truncated at
// `end` is treated as a single raw byte so the scan never reads past the
// buffer.
static uint32_t DecodeUtf8(const char *p, const char *end, int *len)
{
    unsigned char c0 = (unsigned char)p[0];
    size_t n = swinx::UTF8CharLength(c0);
    if (p + n > end)
    {
        *len = 1;
        return c0;
    }
    uint32_t cp = 0;
    swinx::UTF32FromUTF8(p, (unsigned int)n, &cp, 1);
    *len = (int)n;
    return cp;
}

struct SystemFontEntry
{
    FcPattern *pattern; // owning
    FcCharSet *charset; // borrowed from pattern
    int weight;         // FC_WEIGHT_*
    int slant;          // FC_SLANT_*
};

// per-request fallback chain: the primary font's charset decides which
// codepoints need a fallback font.  Owned by the SystemFontList cache.
struct FontFallbackChain
{
    FcPattern *primaryPattern = nullptr; // owning
    FcCharSet *primaryCharset = nullptr; // borrowed from primaryPattern

    ~FontFallbackChain()
    {
        if (primaryPattern)
            FcPatternDestroy(primaryPattern);
    }
};

#ifdef __OHOS__
// preferred families used to resolve the primary font on OHOS, where the
// requested Windows face names cannot be matched
static const char *kOhosCjkFamilies[] = {
    "FZHeiT-SC",
    "FZHeiT-SC-Regular",
    "sans-serif",
};
#endif

class SystemFontList
{
public:
    static SystemFontList &instance()
    {
        static SystemFontList s_list;
        return s_list;
    }

    // spawn the enumeration thread once (thread safe)
    void start()
    {
        bool expected = false;
        if (!m_started.compare_exchange_strong(expected, true))
            return;
        m_quit.store(false, std::memory_order_release); // allow restart after a shutdown
        m_thread = std::thread([this]() { build(); });
    }

    // ask the enumeration thread to stop as soon as it reaches the next
    // checkpoint.  A single FcFontList call already in flight cannot be
    // interrupted, but everything around it (OHOS dir registration, the
    // per-font pattern duplication loop, result publication) is skipped.
    void requestExit() { m_quit.store(true, std::memory_order_release); }

    // signal the enumeration thread to exit, join it and release all font
    // resources held by this object: the enumerated patterns, the cached
    // primary font faces (cairo objects, must go before cairo's static data
    // is reset) and the fallback chains (their primaryPatterns must go before
    // FcFini).  All of this is driven from SConnMgr's destructor, which calls
    // shutdown() before cairo_debug_reset_static_data() / FcFini().
    void shutdown()
    {
        requestExit();
        if (m_thread.joinable())
            m_thread.join();

        {
            std::lock_guard<std::mutex> lock(m_faceMutex);
            for (auto &it : m_faces)
            {
                if (it.second)
                    cairo_font_face_destroy(it.second);
            }
            m_faces.clear();
        }

        {
            std::lock_guard<std::mutex> lock(m_chainMutex);
            m_chains.clear();
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        for (SystemFontEntry &e : m_fonts)
        {
            if (e.pattern)
                FcPatternDestroy(e.pattern);
        }
        m_fonts.clear();
        m_ready.store(false, std::memory_order_release);
    }

    bool ready() const { return m_ready.load(std::memory_order_acquire); }

    // read-only after ready() turns true
    const swinx_stl::vector<SystemFontEntry> &fonts() const { return m_fonts; }

    // Resolve (and cache) the primary font face for `lf` through fontconfig.
    // ApplyFont runs before every text drawing call, and an uncached path
    // would pay a full FcFontMatch (pattern substitution + scoring of every
    // system font) per draw even though the same LOGFONTA always resolves to
    // the same face.  The cache holds one owning reference per entry for the
    // lifetime of the process.  The returned face carries one extra reference
    // the caller must release with cairo_font_face_destroy.
    // Note: a font added later via AddFontResourceExA is visible to requests
    // for new face names (cache miss) but does not invalidate cached faces.
    cairo_font_face_t *getCachedFontFace(const LOGFONTA *lf)
    {
        swinx_stl::string key = lf->lfFaceName[0] ? lf->lfFaceName : "sans-serif";
        key += lf->lfItalic ? "|i" : "|n";
        key += lf->lfWeight > 400 ? "|b" : "|r";

        std::lock_guard<std::mutex> lock(m_faceMutex);
        auto it = m_faces.find(key);
        if (it != m_faces.end())
            return cairo_font_face_reference(it->second);

        cairo_font_face_t *face = createFontFace(lf);
        if (!face)
            return nullptr;

        m_faces.insert(swinx_stl::make_pair(key, face));
        return cairo_font_face_reference(face);
    }

    // Fetch or build the fallback chain for `lf`: the primary font's charset
    // (from a full FcFontMatch) decides which codepoints need a fallback
    // font.  Cached process wide since the result only depends on the request.
    std::shared_ptr<FontFallbackChain> getChain(const LOGFONTA *lf, bool cjk)
    {
        int weight = lf->lfWeight > 400 ? FC_WEIGHT_BOLD : FC_WEIGHT_NORMAL;
        int slant = lf->lfItalic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN;
        swinx_stl::string key = lf->lfFaceName[0] ? lf->lfFaceName : "sans-serif";
        key += cjk ? "|zh" : "|--";
        key += "|w" + std::to_string(weight);
        key += "|s" + std::to_string(slant);

        std::lock_guard<std::mutex> lock(m_chainMutex);
        auto it = m_chains.find(key);
        if (it != m_chains.end())
            return it->second;

        auto chain = std::make_shared<FontFallbackChain>();

        // shutting down: skip the expensive FcFontMatch. A pattern allocated
        // now would be published into m_chains after shutdown() already ran,
        // and would only be destroyed after FcFini in ~SConnMgr.
        if (m_quit.load(std::memory_order_acquire))
            return chain;

#ifdef __OHOS__
        // requested Windows face names cannot be matched on OHOS; resolve with
        // the preferred CJK families instead
        FcPattern *pat = BuildFontMatchPattern(lf, cjk, kOhosCjkFamilies,
                                               ARRAYSIZE(kOhosCjkFamilies), TRUE);
#else
        FcPattern *pat = BuildFontMatchPattern(lf, cjk, NULL, 0, FALSE);
#endif
        if (pat)
        {
            FcResult result = FcResultNoMatch;
            chain->primaryPattern = FcFontMatch(NULL, pat, &result);
            FcPatternDestroy(pat);
            if (chain->primaryPattern)
            {
                FcCharSet *cs = NULL;
                if (FcPatternGetCharSet(chain->primaryPattern, FC_CHARSET, 0, &cs) == FcResultMatch)
                    chain->primaryCharset = cs;
            }
        }
        m_chains.insert(swinx_stl::make_pair(key, chain));
        return chain;
    }

private:
    SystemFontList(){
        FcInit();
        #ifdef __OHOS__
        const char *fontDirs[] = {
            "/system/fonts",
            "/system/font",
            "/vendor/fonts",
            "/hw_product/fonts",
        };
        FcConfig *config = FcConfigGetCurrent();
        if (config){
            for (const char *dir : fontDirs)
                FcConfigAppFontAddDir(config, (const FcChar8 *)dir);
            FcConfigBuildFonts(config);
        }
        #endif//__OHOS__
    }
    ~SystemFontList()
    {
        shutdown();
    }

    // create the primary font face for `lf` by resolving the request through
    // fontconfig and creating an FT font face from the matched pattern (same
    // resolution the cairo toy API uses, but we also get the charset needed
    // for glyph-level fallback)
    cairo_font_face_t *createFontFace(const LOGFONTA *lf)
    {
        bool cjk = SwinXIsCjkFontAlias(lf->lfFaceName);
#ifdef __OHOS__
        FcPattern *pat = BuildFontMatchPattern(lf, cjk, kOhosCjkFamilies,
                                               ARRAYSIZE(kOhosCjkFamilies), TRUE);
#else
        FcPattern *pat = BuildFontMatchPattern(lf, cjk, NULL, 0, FALSE);
#endif
        if (!pat)
            return nullptr;

        FcResult result = FcResultNoMatch;
        FcPattern *matched = FcFontMatch(NULL, pat, &result);
        FcPatternDestroy(pat);
        if (!matched)
            return nullptr;

        cairo_font_face_t *face = cairo_ft_font_face_create_for_pattern(matched);
        FcPatternDestroy(matched);
        if (!face || cairo_font_face_status(face) != CAIRO_STATUS_SUCCESS)
        {
            if (face)
                cairo_font_face_destroy(face);
            return nullptr;
        }
        return face;
    }

    void build()
    {
        if (m_quit.load(std::memory_order_acquire))
            return; // shutdown arrived before the thread even started

        FcPattern *pat = FcPatternCreate();
        FcObjectSet *os = FcObjectSetBuild(FC_FAMILY, FC_FILE, FC_INDEX, FC_CHARSET, FC_WEIGHT, FC_SLANT, (const char *)NULL);
        FcFontSet *set = (pat && os) ? FcFontList(FcConfigGetCurrent(), pat, os) : NULL;
        if (os)
            FcObjectSetDestroy(os);
        if (pat)
            FcPatternDestroy(pat);

        if (set)
        {
            swinx_stl::vector<SystemFontEntry> fonts;
            fonts.reserve(set->nfont);
            for (int i = 0; i < set->nfont; i++)
            {
                if (m_quit.load(std::memory_order_relaxed))
                    break; // shutdown pending: stop duplicating patterns
                FcPattern *f = set->fonts[i];
                FcCharSet *cs = NULL;
                if (FcPatternGetCharSet(f, FC_CHARSET, 0, &cs) != FcResultMatch || !cs)
                    continue; // without charset info the entry is useless
                FcPattern *dup = FcPatternDuplicate(f);
                if (!dup)
                    continue;
                SystemFontEntry e;
                e.pattern = dup;
                e.charset = cs; // borrowed from the duplicate
                if (FcPatternGetInteger(dup, FC_WEIGHT, 0, &e.weight) != FcResultMatch)
                    e.weight = FC_WEIGHT_NORMAL;
                if (FcPatternGetInteger(dup, FC_SLANT, 0, &e.slant) != FcResultMatch)
                    e.slant = FC_SLANT_ROMAN;
                fonts.push_back(e);
            }
            FcFontSetDestroy(set);

            if (m_quit.load(std::memory_order_acquire))
            {
                // shutdown raced the enumeration: these duplicates were never
                // published to m_fonts, so nobody else can release them
                for (SystemFontEntry &e : fonts)
                {
                    if (e.pattern)
                        FcPatternDestroy(e.pattern);
                }
                SLOG_STMD() << "font fallback enumeration aborted by shutdown";
                return;
            }

            std::lock_guard<std::mutex> lock(m_mutex);
            m_fonts.swap(fonts);
            m_ready.store(true, std::memory_order_release);
            //SLOG_STMI() << "font fallback enumeration done: " << (int)m_fonts.size() << " fonts";
        }
        else
        {
            SLOG_STMW() << "font fallback enumeration failed, glyph fallback disabled";
        }
    }

    std::thread m_thread;
    std::atomic<bool> m_started{false};
    std::atomic<bool> m_quit{false};
    std::atomic<bool> m_ready{false};
    mutable std::mutex m_mutex;    // guards m_fonts / m_chains release
    swinx_stl::vector<SystemFontEntry> m_fonts;
    std::mutex m_faceMutex;        // guards the face cache
    swinx_stl::map<swinx_stl::string, cairo_font_face_t *> m_faces;
    std::mutex m_chainMutex;       // guards the chain cache
    swinx_stl::map<swinx_stl::string, std::shared_ptr<FontFallbackChain>> m_chains;
};

void SwinXFontFallbackPrefetch()
{
    SystemFontList::instance().start();
}

void SwinXFontFallbackShutdown()
{
    SystemFontList::instance().shutdown();
}

//////////////////////////////////////////////////////////////////////////////
// per-context fallback data

// per cairo_t data: primary charset + scaled fonts created on demand for the
// system fonts actually used by this context
struct FontFallbackCtx
{
    swinx_stl::string key; // request this ctx was built for (incl. font matrix)
    std::shared_ptr<FontFallbackChain> chain;
    int reqWeight = FC_WEIGHT_NORMAL; // request class used for tiering
    int reqSlant = FC_SLANT_ROMAN;
    swinx_stl::map<int, cairo_scaled_font_t *> scaledByIndex; // index in system list -> scaled font (owned)
    int lastFallbackHit = -1;                           // index into the system font list

    ~FontFallbackCtx()
    {
        for (auto &it : scaledByIndex)
        {
            if (it.second)
                cairo_scaled_font_destroy(it.second);
        }
    }
};

static void FontFallbackCtxDestroy(void *p)
{
    delete (FontFallbackCtx *)p;
}

// Matching preference: fonts with the same weight/slant class as the request
// win over everything else, so a bold request falls back to bold fonts first.
static inline int TierOf(const SystemFontEntry &e, int reqWeight, int reqSlant)
{
    bool eBold = e.weight >= FC_WEIGHT_BOLD;
    bool eItalic = e.slant != FC_SLANT_ROMAN;
    bool rBold = reqWeight >= FC_WEIGHT_BOLD;
    bool rItalic = reqSlant != FC_SLANT_ROMAN;
    if (eBold == rBold && eItalic == rItalic)
        return 0;
    if (eBold == rBold)
        return 1;
    if (eItalic == rItalic)
        return 2;
    return 3;
}

// Create (once) and return the scaled font for system font `idx`, using the
// context's current font matrix / ctm / options.  Returns NULL on failure
// (the failure is cached, so a broken font is not retried on every draw).
static cairo_scaled_font_t *GetScaledFont(FontFallbackCtx *ctx, cairo_t *cr, int idx)
{
    auto it = ctx->scaledByIndex.find(idx);
    if (it != ctx->scaledByIndex.end())
        return it->second; // may be NULL = known failure

    const swinx_stl::vector<SystemFontEntry> &fonts = SystemFontList::instance().fonts();
    if (idx < 0 || idx >= (int)fonts.size())
        return NULL;

    cairo_font_face_t *face = cairo_ft_font_face_create_for_pattern(fonts[idx].pattern);
    cairo_scaled_font_t *scaled = NULL;
    if (face)
    {
        cairo_matrix_t fontMtx, ctm;
        cairo_font_options_t *opts = cairo_font_options_create();
        cairo_get_font_matrix(cr, &fontMtx);
        cairo_get_matrix(cr, &ctm); // current transformation matrix == CTM
        cairo_get_font_options(cr, opts);
        scaled = cairo_scaled_font_create(face, &fontMtx, &ctm, opts);
        cairo_font_options_destroy(opts);
        cairo_font_face_destroy(face);
    }
    ctx->scaledByIndex[idx] = scaled;
    return scaled;
}

void AttachFontFallback(cairo_t *cr, const LOGFONTA *lf)
{
    if (!cr || !lf)
        return;

    SystemFontList &list = SystemFontList::instance();
    list.start(); // idempotent; normally already running since SConnMgr init
    if (!list.ready())
        return; // enumeration still running: keep single-font behaviour,
                // the next draw will attach the full fallback data

    const char *family = lf->lfFaceName[0] ? lf->lfFaceName : "sans-serif";
    bool cjk = SwinXIsCjkFontAlias(lf->lfFaceName);
    int weight = lf->lfWeight > 400 ? FC_WEIGHT_BOLD : FC_WEIGHT_NORMAL;
    int slant = lf->lfItalic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN;

    // ApplyFont runs before every text drawing call, so the early return
    // below avoids redoing the work for every draw.  The ctx key covers the
    // chain (family/cjk/weight/slant) and the font matrix (size).
    swinx_stl::string key = family;
    key += cjk ? "|zh" : "|--";
    key += "|w" + std::to_string(weight);
    key += "|s" + std::to_string(slant);
    cairo_matrix_t fontMtx;
    cairo_get_font_matrix(cr, &fontMtx);
    swinx_stl::string ctxKey = key + "|m" + std::to_string(fontMtx.xx) + "," + std::to_string(fontMtx.yy);

    // fast path: same request already attached to this context
    if (FontFallbackCtx *old = (FontFallbackCtx *)cairo_get_user_data(cr, &kFontFallbackKey))
    {
        if (old->key == ctxKey)
            return;
    }

    // 1. lookup / build the primary charset (cached process wide, the
    //    primary charset does not depend on the font matrix)
    std::shared_ptr<FontFallbackChain> chain = list.getChain(lf, cjk);

    // 2. attach per-context data (scaled fonts are created on demand in
    //    SplitTextRuns, so a system with hundreds of fonts costs nothing up
    //    front).  cairo_set_user_data replaces and destroys any previous
    //    user data when the font changed.
    FontFallbackCtx *ctx = new FontFallbackCtx();
    ctx->key = ctxKey;
    ctx->chain = chain;
    ctx->reqWeight = weight;
    ctx->reqSlant = slant;
    cairo_status_t st = cairo_set_user_data(cr, &kFontFallbackKey, ctx, FontFallbackCtxDestroy);
    if (st != CAIRO_STATUS_SUCCESS)
    {
        delete ctx;
        SLOG_STMW() << "attach font fallback failed: " << cairo_status_to_string(st);
    }
}

void SplitTextRuns(cairo_t *cr, const char *utf8, int len, swinx_stl::vector<TextRun> &runs)
{
    runs.clear();
    if (len <= 0)
        return;

    FontFallbackCtx *ctx = NULL;
    if (cr)
    {
        FontFallbackCtx *d = (FontFallbackCtx *)cairo_get_user_data(cr, &kFontFallbackKey);
        if (d && SystemFontList::instance().ready())
            ctx = d;
    }
    if (!ctx)
    {
        TextRun run;
        run.offset = 0;
        run.len = len;
        run.scaled = NULL;
        runs.push_back(run);
        return;
    }

    FontFallbackChain *chain = ctx->chain.get();
    const swinx_stl::vector<SystemFontEntry> &fonts = SystemFontList::instance().fonts();
    auto pickFont = [&](uint32_t cp) -> int {
        if (cp < 0x20)
            return -1; // control characters always use the primary font
        if (chain->primaryCharset && FcCharSetHasChar(chain->primaryCharset, cp))
            return -1; // note: lastFallbackHit is kept sticky on purpose, it
                       // stays valid for the next primary-miss codepoint
        // fast path: the fallback that matched the previous lookup usually
        // covers adjacent glyphs of the same script (CJK runs, emoji runs...)
        int last = ctx->lastFallbackHit;
        if (last >= 0 && last < (int)fonts.size())
        {
            FcCharSet *fcs = fonts[last].charset;
            if (fcs && FcCharSetHasChar(fcs, cp))
                return last;
        }
        // full scan: same weight/slant class as the request first
        for (int tier = 0; tier < 4; tier++)
        {
            for (size_t k = 0; k < fonts.size(); ++k)
            {
                if (TierOf(fonts[k], ctx->reqWeight, ctx->reqSlant) != tier)
                    continue;
                if (fonts[k].charset && FcCharSetHasChar(fonts[k].charset, cp))
                {
                    ctx->lastFallbackHit = (int)k; // remember for the next lookup
                    return (int)k;
                }
            }
        }
        return -1; // nobody covers it: keep the primary font (tofu as before)
    };
    auto fontOf = [&](int idx) -> cairo_scaled_font_t * {
        return idx < 0 ? NULL : GetScaledFont(ctx, cr, idx);
    };

    int runStart = 0;
    int curFont = -2; // -2 = not initialized yet

    int i = 0;
    while (i < len)
    {
        int cpLen = 0;
        uint32_t cp = DecodeUtf8(utf8 + i, utf8 + len, &cpLen);
        int fi = pickFont(cp);
        if (curFont == -2)
        {
            curFont = fi;
            runStart = i;
        }
        else if (fi != curFont)
        {
            TextRun run;
            run.offset = runStart;
            run.len = i - runStart;
            run.scaled = fontOf(curFont);
            runs.push_back(run);
            curFont = fi;
            runStart = i;
        }
        i += cpLen;
    }
    if (curFont == -2)
        curFont = -1;
    TextRun run;
    run.offset = runStart;
    run.len = len - runStart;
    run.scaled = fontOf(curFont);
    runs.push_back(run);
}

cairo_font_face_t *SwinXGetCachedFontFace(const LOGFONTA *lf)
{
    return SystemFontList::instance().getCachedFontFace(lf);
}
