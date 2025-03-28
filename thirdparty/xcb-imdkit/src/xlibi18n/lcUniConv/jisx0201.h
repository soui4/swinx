
/*
 * JISX0201.1976-0
 */

static int
jisx0201_mbtowc (ucs4_t *pwc, const unsigned char *s, int n)
{
  unsigned char c = *s;
  if (c < 0x80) {
    if (c == 0x5c)
      *pwc = (ucs4_t) 0x00a5;
    else if (c == 0x7e)
      *pwc = (ucs4_t) 0x203e;
    else
      *pwc = (ucs4_t) c;
    return 1;
  } else {
    if (c >= 0xa1 && c < 0xe0) {
      *pwc = (ucs4_t) c + 0xfec0;
      return 1;
    }
  }
  return RET_ILSEQ;
}

static int
jisx0201_wctomb (unsigned char *r, ucs4_t wc, int n)
{
  if (wc < 0x0080 && !(wc == 0x005c || wc == 0x007e)) {
    *r = wc;
    return 1;
  }
  if (wc == 0x00a5) {
    *r = 0x5c;
    return 1;
  }
  if (wc == 0x203e) {
    *r = 0x7e;
    return 1;
  }
  if (wc >= 0xff61 && wc < 0xffa0) {
    *r = wc - 0xfec0;
    return 1;
  }
  return RET_ILSEQ;
}
