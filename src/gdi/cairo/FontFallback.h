#ifndef _FONT_FALLBACK_H_
#define _FONT_FALLBACK_H_

#include <cairo.h>
#include <windows.h>
#include <vector>

// forward declaration (fontconfig.h may or may not be included by the user)
struct _FcPattern;
typedef struct _FcPattern FcPattern;

// Per-run text segmentation result.
// `scaled == NULL` means: use the context's current scaled font (the primary font).
struct TextRun
{
    int offset;                  // byte offset into the utf8 string
    int len;                     // byte length
    cairo_scaled_font_t *scaled; // fallback scaled font, or NULL for primary
};

// Start the background system-font enumeration thread.  The thread lists all
// fonts known to fontconfig (family/charset/weight/slant) once and publishes
// them for the fallback search, so no text draw ever blocks on a full font
// scan.  Idempotent; called automatically when SConnMgr is created and on the
// first AttachFontFallback call.
void SwinXFontFallbackPrefetch();

// Signal the enumeration thread to exit at the next checkpoint and join it,
// then release the enumerated font patterns.  Does not wait for a full
// enumeration to finish: an in-flight FcFontList call completes on its own,
// everything after it is skipped and partial results are discarded.
// Must be called before FcFini().  Idempotent.
void SwinXFontFallbackShutdown();

// True when the requested face name looks like a CJK font alias: Windows font
// names (SimSun, Microsoft YaHei, ...), generic names, or any non-ASCII name
// (e.g. Chinese face names).  Such requests usually cannot be satisfied by a
// single Linux/OHOS font, so the fallback chain gets a zh language hint.
bool SwinXIsCjkFontAlias(const char *faceName);

// Resolve (and cache process wide) the primary font face for `lf` through
// fontconfig.  ApplyFont runs before every text drawing call, and the cache
// skips the FcFontMatch round trip for repeated requests.  The returned face
// carries one extra reference which the caller must release with
// cairo_font_face_destroy.  Returns NULL on failure.
cairo_font_face_t *SwinXGetCachedFontFace(const LOGFONTA *lf);

// Build the fontconfig match pattern used to resolve the primary font.
// Both the font-face creation code and the fallback chain builder use this so
// the fallback chain always agrees with the selected primary font.
//
//   lf              - the LOGFONTA describing the request
//   zhHint          - add FC_LANG=zh-cn (biases match towards CJK coverage)
//   extraFamilies   - additional family names added before the raw face name
//                     (platform specific preferred families, may be NULL)
//   nExtraFamilies  - number of entries in extraFamilies
//   zhCharsetHint   - add FC_CHARSET=U+4E2D hint (OHOS style)
FcPattern *BuildFontMatchPattern(const LOGFONTA *lf, bool zhHint,
                                 const char *const *extraFamilies, int nExtraFamilies,
                                 bool zhCharsetHint);

// Build (or fetch from the process-wide cache) the primary-font charset for
// `lf` and attach it to `cr` as cairo user data, together with lazily created
// scaled fonts for the enumerated system fonts.  Must be called after the
// primary font face and size have been selected on `cr`.  Platform specific
// pattern hints (OHOS preferred CJK families) are applied internally.
//
// While the background enumeration is still running this call does nothing
// (single-font behaviour) and simply succeeds on a later draw.
void AttachFontFallback(cairo_t *cr, const LOGFONTA *lf);

// Split utf8 text into per-font runs using the data attached to `cr`.
// Always fills `runs` with at least one entry covering [0, len).
// When nothing is attached, a single run with scaled==NULL is returned.
void SplitTextRuns(cairo_t *cr, const char *utf8, int len, swinx_stl::vector<TextRun> &runs);

#endif //_FONT_FALLBACK_H_
