#ifndef _UNI_CONV_H_
#define _UNI_CONV_H_
#include <ctypes.h>
#include <assert.h>

namespace swinx {
	enum { SURROGATE_LEAD_FIRST = 0xD800 };
	enum { SURROGATE_LEAD_LAST = 0xDBFF };
	enum { SURROGATE_TRAIL_FIRST = 0xDC00 };
	enum { SURROGATE_TRAIL_LAST = 0xDFFF };
	enum { SUPPLEMENTAL_PLANE_FIRST = 0x10000 };

	size_t UTF8CharLength(unsigned char ch);
	inline size_t UTF16CharLength(uint16_t uch) {
		return ((uch >= SURROGATE_LEAD_FIRST) && (uch <= SURROGATE_LEAD_LAST)) ? 2 : 1;
	}

	size_t UTF16toUTF8Length(const uint16_t* uptr, unsigned int tlen);
	size_t UTF8FromUTF16(const uint16_t* uptr, unsigned int tlen, char* putf, unsigned int len);
	size_t UTF16Length(const char* s, size_t len);
	size_t UTF16FromUTF8(const char* s, size_t len, uint16_t* tbuf, size_t tlen);

	size_t UTF32FromUTF8(const char *s, unsigned int len, uint32_t *tbuf, unsigned int tlen);
	inline size_t UTF32CharLength(uint32_t uch) {
		return 1;
	}
	size_t UTF8FromUTF32(const uint32_t *uptr, unsigned int tlen, char *putf, unsigned int len);
	size_t UTF32Length(const char* s, size_t len) ;
	size_t UTF32toUTF8Length(const uint32_t* uptr, unsigned int tlen);

	inline size_t WideCharLength(wchar_t ch) {
#if (WCHAR_SIZE==2)
		assert(sizeof(wchar_t) == 2);
		return UTF16CharLength(ch);
#else
		assert(sizeof(wchar_t) == 4);
		return UTF32CharLength(ch);
#endif
	}

}

#endif//_UNI_CONV_H_