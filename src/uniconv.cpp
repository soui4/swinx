#include "uniconv.h"
#include "stdio.h"

namespace swinx
{

size_t UTF8CharLength(unsigned char ch) {
    if (ch < 0x80) {
        return 1;
    }
    else if (ch < 0x80 + 0x40 + 0x20) {
        return 2;
    }
    else if (ch < 0x80 + 0x40 + 0x20 + 0x10) {
        return 3;
    }
    else {
        return 4;
    }
}


size_t UTF16toUTF8Length(const uint16_t* uptr, unsigned int tlen) {
    size_t len = 0;
    for (unsigned int i = 0; i < tlen && uptr[i];) {
        unsigned int uch = uptr[i];
        if (uch < 0x80) {
            len++;
        }
        else if (uch < 0x800) {
            len += 2;
        }
        else if ((uch >= SURROGATE_LEAD_FIRST) &&
            (uch <= SURROGATE_TRAIL_LAST)) {
            len += 4;
            i++;
        }
        else {
            len += 3;
        }
        i++;
    }
    return len;
}

size_t UTF8FromUTF16(const uint16_t* uptr, unsigned int tlen, char* putf, unsigned int len) {
    unsigned int k = 0;
    for (unsigned int i = 0; i < tlen && uptr[i];) {
        unsigned int uch = uptr[i];
        if (uch < 0x80) {
            putf[k++] = static_cast<char>(uch);
        }
        else if (uch < 0x800) {
            putf[k++] = static_cast<char>(0xC0 | (uch >> 6));
            putf[k++] = static_cast<char>(0x80 | (uch & 0x3f));
        }
        else if ((uch >= SURROGATE_LEAD_FIRST) &&
            (uch <= SURROGATE_TRAIL_LAST)) {
            // Half a surrogate pair
            i++;
            unsigned int xch = 0x10000 + ((uch & 0x3ff) << 10) + (uptr[i] & 0x3ff);
            putf[k++] = static_cast<char>(0xF0 | (xch >> 18));
            putf[k++] = static_cast<char>(0x80 | ((xch >> 12) & 0x3f));
            putf[k++] = static_cast<char>(0x80 | ((xch >> 6) & 0x3f));
            putf[k++] = static_cast<char>(0x80 | (xch & 0x3f));
        }
        else {
            putf[k++] = static_cast<char>(0xE0 | (uch >> 12));
            putf[k++] = static_cast<char>(0x80 | ((uch >> 6) & 0x3f));
            putf[k++] = static_cast<char>(0x80 | (uch & 0x3f));
        }
        i++;
    }
    if (k < len)
        putf[k] = '\0';
    return k;
}


size_t UTF16Length(const char* s, size_t len) {
    size_t ulen = 0;
    size_t charLen;
    for (size_t i = 0; i < len;) {
        unsigned char ch = static_cast<unsigned char>(s[i]);
        if (ch < 0x80) {
            charLen = 1;
        }
        else if (ch < 0x80 + 0x40 + 0x20) {
            charLen = 2;
        }
        else if (ch < 0x80 + 0x40 + 0x20 + 0x10) {
            charLen = 3;
        }
        else {
            charLen = 4;
            ulen++;
        }
        i += charLen;
        ulen++;
    }
    return ulen;
}

size_t UTF16FromUTF8(const char* s, size_t len, uint16_t* tbuf, size_t tlen) {
    size_t ui = 0;
    const unsigned char* us = reinterpret_cast<const unsigned char*>(s);
    size_t i = 0;
    while ((i < len) && (ui < tlen)) {
        unsigned char ch = us[i++];
        if (ch < 0x80) {
            tbuf[ui] = ch;
        }
        else if (ch < 0x80 + 0x40 + 0x20) {
            tbuf[ui] = static_cast<uint16_t>((ch & 0x1F) << 6);
            ch = us[i++];
            tbuf[ui] = static_cast<uint16_t>(tbuf[ui] + (ch & 0x7F));
        }
        else if (ch < 0x80 + 0x40 + 0x20 + 0x10) {
            tbuf[ui] = static_cast<uint16_t>((ch & 0xF) << 12);
            ch = us[i++];
            tbuf[ui] = static_cast<uint16_t>(tbuf[ui] + ((ch & 0x7F) << 6));
            ch = us[i++];
            tbuf[ui] = static_cast<uint16_t>(tbuf[ui] + (ch & 0x7F));
        }
        else {
            // Outside the BMP so need two surrogates
            int val = (ch & 0x7) << 18;
            ch = us[i++];
            val += (ch & 0x3F) << 12;
            ch = us[i++];
            val += (ch & 0x3F) << 6;
            ch = us[i++];
            val += (ch & 0x3F);
            tbuf[ui] = static_cast<uint16_t>(((val - 0x10000) >> 10) + SURROGATE_LEAD_FIRST);
            ui++;
            tbuf[ui] = static_cast<uint16_t>((val & 0x3ff) + SURROGATE_TRAIL_FIRST);
        }
        ui++;
    }
    if (ui < tlen) {
        tbuf[ui] = 0;
    }
    return ui;
}

size_t UTF32FromUTF8(const char *s, unsigned int len, uint32_t *tbuf, unsigned int tlen) {
	size_t ui=0;
	const unsigned char *us = reinterpret_cast<const unsigned char *>(s);
	unsigned int i=0;
	while ((i<len) && (ui<tlen)) {
		unsigned char ch = us[i++];
		unsigned int value = 0;
		if (ch < 0x80) {
			value = ch;
		} else if (ch < 0x80 + 0x40 + 0x20) {
			value = (ch & 0x1F) << 6;
			ch = us[i++];
			value += ch & 0x7F;
		} else if (ch < 0x80 + 0x40 + 0x20 + 0x10) {
			value = (ch & 0xF) << 12;
			ch = us[i++];
			value += (ch & 0x7F) << 6;
			ch = us[i++];
			value += ch & 0x7F;
		} else  {
			value = (ch & 0x7) << 18;
			ch = us[i++];
			value += (ch & 0x3F) << 12;
			ch = us[i++];
			value += (ch & 0x3F) << 6;
			ch = us[i++];
			value += ch & 0x3F;
		}
		tbuf[ui] = value;
		ui++;
	}
    if(ui<tlen){
        tbuf[ui]=0;
    }
	return ui;
}

size_t UTF8FromUTF32(const uint32_t *uptr, unsigned int tlen, char *putf, unsigned int len)
{
    unsigned int k = 0;
    for (unsigned int i = 0; i < tlen && uptr[i];) {
        unsigned int uch = uptr[i];
        if (uch < 0x80) {
            putf[k++] = static_cast<char>(uch);
        }
        else if (uch < 0x800) {
            putf[k++] = static_cast<char>(0xC0 | (uch >> 6));
            putf[k++] = static_cast<char>(0x80 | (uch & 0x3f));
        }
        else if (uch < 0x10000) {
            putf[k++] = static_cast<char>(0xE0 | (uch >> 12));
            putf[k++] = static_cast<char>(0x80 | ((uch >> 6) & 0x3f));
            putf[k++] = static_cast<char>(0x80 | (uch & 0x3f));
        }
        else if (uch < 0x200000) {
            putf[k++] = static_cast<char>(0xF0 | (uch >> 18));
            putf[k++] = static_cast<char>(0x80 | ((uch >> 12) & 0x3f));
            putf[k++] = static_cast<char>(0x80 | ((uch >> 6) & 0x3f));
            putf[k++] = static_cast<char>(0x80 | (uch & 0x3f));
        }
        else {
            printf("invalid utf32 code=0x%08x", uch);
        }
        i++;
    }
    if (k < len)
        putf[k] = '\0';
    return k;
}


size_t UTF32Length(const char* s, size_t len) {
    size_t ulen = 0;
    size_t charLen;
    for (size_t i = 0; i < len;) {
        unsigned char ch = static_cast<unsigned char>(s[i]);
        if (ch < 0x80) {
            charLen = 1;
        }
        else if (ch < 0x80 + 0x40 + 0x20) {
            charLen = 2;
        }
        else if (ch < 0x80 + 0x40 + 0x20 + 0x10) {
            charLen = 3;
        }
        else {
            charLen = 4;
        }
        i += charLen;
        ulen++;
    }
    return ulen;
}

size_t UTF32toUTF8Length(const uint32_t* uptr, unsigned int tlen){
    size_t len = 0;
    for (unsigned int i = 0; i < tlen && uptr[i];) {
        unsigned int uch = uptr[i];
        if (uch < 0x80) {
            len++;
        }
        else if (uch < 0x800) {
            len += 2;
        }
        else if (uch < 0x10000)
        {
            len += 3;
        }
        else if (uch <0x200000) {
            len += 4;
        }
        else {
            printf("invalid utf32 code=0x%08x", uch);
        }
        i++;
    }
    return len;
}

}