
/*
 * ARMSCII-8
 */

static const unsigned short armscii_8_2uni[96] = {
  /* 0xa0 */
  0x00a0, 0xfffd, 0x0587, 0x0589, 0x0029, 0x0028, 0x00bb, 0x00ab,
  0x2014, 0x002e, 0x055d, 0x002c, 0x002d, 0x058a, 0x2026, 0x055c,
  /* 0xb0 */
  0x055b, 0x055e, 0x0531, 0x0561, 0x0532, 0x0562, 0x0533, 0x0563,
  0x0534, 0x0564, 0x0535, 0x0565, 0x0536, 0x0566, 0x0537, 0x0567,
  /* 0xc0 */
  0x0538, 0x0568, 0x0539, 0x0569, 0x053a, 0x056a, 0x053b, 0x056b,
  0x053c, 0x056c, 0x053d, 0x056d, 0x053e, 0x056e, 0x053f, 0x056f,
  /* 0xd0 */
  0x0540, 0x0570, 0x0541, 0x0571, 0x0542, 0x0572, 0x0543, 0x0573,
  0x0544, 0x0574, 0x0545, 0x0575, 0x0546, 0x0576, 0x0547, 0x0577,
  /* 0xe0 */
  0x0548, 0x0578, 0x0549, 0x0579, 0x054a, 0x057a, 0x054b, 0x057b,
  0x054c, 0x057c, 0x054d, 0x057d, 0x054e, 0x057e, 0x054f, 0x057f,
  /* 0xf0 */
  0x0550, 0x0580, 0x0551, 0x0581, 0x0552, 0x0582, 0x0553, 0x0583,
  0x0554, 0x0584, 0x0555, 0x0585, 0x0556, 0x0586, 0x055a, 0xfffd,
};

static int
armscii_8_mbtowc (ucs4_t *pwc, const unsigned char *s, int n)
{
  unsigned char c = *s;
  if (c < 0xa0) {
    *pwc = (ucs4_t) c;
    return 1;
  }
  else {
    unsigned short wc = armscii_8_2uni[c-0xa0];
    if (wc != 0xfffd) {
      *pwc = (ucs4_t) wc;
      return 1;
    }
  }
  return RET_ILSEQ;
}

static const unsigned char armscii_8_page00[8] = {
  0xa5, 0xa4, 0x2a, 0x2b, 0xab, 0xac, 0xa9, 0x2f, /* 0x28-0x2f */
};
static const unsigned char armscii_8_page00_1[32] = {
  0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xa0-0xa7 */
  0x00, 0x00, 0x00, 0xa7, 0x00, 0x00, 0x00, 0x00, /* 0xa8-0xaf */
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xb0-0xb7 */
  0x00, 0x00, 0x00, 0xa6, 0x00, 0x00, 0x00, 0x00, /* 0xb8-0xbf */
};
static const unsigned char armscii_8_page05[96] = {
  0x00, 0xb2, 0xb4, 0xb6, 0xb8, 0xba, 0xbc, 0xbe, /* 0x30-0x37 */
  0xc0, 0xc2, 0xc4, 0xc6, 0xc8, 0xca, 0xcc, 0xce, /* 0x38-0x3f */
  0xd0, 0xd2, 0xd4, 0xd6, 0xd8, 0xda, 0xdc, 0xde, /* 0x40-0x47 */
  0xe0, 0xe2, 0xe4, 0xe6, 0xe8, 0xea, 0xec, 0xee, /* 0x48-0x4f */
  0xf0, 0xf2, 0xf4, 0xf6, 0xf8, 0xfa, 0xfc, 0x00, /* 0x50-0x57 */
  0x00, 0x00, 0xfe, 0xb0, 0xaf, 0xaa, 0xb1, 0x00, /* 0x58-0x5f */
  0x00, 0xb3, 0xb5, 0xb7, 0xb9, 0xbb, 0xbd, 0xbf, /* 0x60-0x67 */
  0xc1, 0xc3, 0xc5, 0xc7, 0xc9, 0xcb, 0xcd, 0xcf, /* 0x68-0x6f */
  0xd1, 0xd3, 0xd5, 0xd7, 0xd9, 0xdb, 0xdd, 0xdf, /* 0x70-0x77 */
  0xe1, 0xe3, 0xe5, 0xe7, 0xe9, 0xeb, 0xed, 0xef, /* 0x78-0x7f */
  0xf1, 0xf3, 0xf5, 0xf7, 0xf9, 0xfb, 0xfd, 0xa2, /* 0x80-0x87 */
  0x00, 0xa3, 0xad, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x88-0x8f */
};
static const unsigned char armscii_8_page20[24] = {
  0x00, 0x00, 0x00, 0x00, 0xa8, 0x00, 0x00, 0x00, /* 0x10-0x17 */
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x18-0x1f */
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xae, 0x00, /* 0x20-0x27 */
};

static int
armscii_8_wctomb (unsigned char *r, ucs4_t wc, int n)
{
  unsigned char c = 0;
  if (wc < 0x0028) {
    *r = wc;
    return 1;
  }
  else if (wc >= 0x0028 && wc < 0x0030)
    c = armscii_8_page00[wc-0x0028];
  else if (wc >= 0x0030 && wc < 0x00a0)
    c = wc;
  else if (wc >= 0x00a0 && wc < 0x00c0)
    c = armscii_8_page00_1[wc-0x00a0];
  else if (wc >= 0x0530 && wc < 0x0590)
    c = armscii_8_page05[wc-0x0530];
  else if (wc >= 0x2010 && wc < 0x2028)
    c = armscii_8_page20[wc-0x2010];
  if (c != 0) {
    *r = c;
    return 1;
  }
  return RET_ILSEQ;
}
