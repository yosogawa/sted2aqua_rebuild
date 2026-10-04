#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

/*
  Kanji conversion for STed2.

  The 1997 nkf 1.7 embed (EUC/SJIS/JIS only, no UTF-8) is replaced with
  POSIX iconv. Call sites keep nkf_conv / nkf_convert.
*/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iconv.h>

#include "nkflib.h"

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

static int
mode_has(const char *mode, const char *key)
{
  return mode != NULL && strstr(mode, key) != NULL;
}

static const char *
iconv_encoding(const char *mode)
{
  if (mode == NULL || mode[0] == '\0')
    return "SHIFT_JIS";
  if (mode_has(mode, "UTF-8") || mode_has(mode, "utf-8") ||
      mode_has(mode, "UTF8") || mode_has(mode, "utf8"))
    return "UTF-8";
  /* Check SJIS before JIS: strstr("SJIS","JIS") would otherwise match. */
  if (mode_has(mode, "SHIFT_JIS") || mode_has(mode, "shift_jis") ||
      mode_has(mode, "SJIS") || mode_has(mode, "sjis") ||
      mode_has(mode, "CP932") || mode_has(mode, "cp932"))
    return "SHIFT_JIS";
  if (mode_has(mode, "EUC") || mode_has(mode, "euc") ||
      mode_has(mode, "ujis") || mode_has(mode, "UJIS"))
    return "EUC-JP";
  if (mode_has(mode, "ISO-2022") || mode_has(mode, "iso-2022") ||
      strcmp(mode, "JIS") == 0 || strcmp(mode, "jis") == 0 ||
      mode_has(mode, "JISK") || mode_has(mode, "jisk"))
    return "ISO-2022-JP";
  return mode;
}

static iconv_t
open_cd(const char *tocode, const char *fromcode)
{
  static const char *sjis_names[] = { "SHIFT_JIS", "CP932", "SJIS", NULL };
  const char **to_list, **from_list;
  const char *to_plain[2];
  const char *from_plain[2];
  int i, j;
  iconv_t cd;

  if (strcmp(tocode, "SHIFT_JIS") == 0)
    to_list = sjis_names;
  else {
    to_plain[0] = tocode;
    to_plain[1] = NULL;
    to_list = to_plain;
  }
  if (strcmp(fromcode, "SHIFT_JIS") == 0)
    from_list = sjis_names;
  else {
    from_plain[0] = fromcode;
    from_plain[1] = NULL;
    from_list = from_plain;
  }

  for (i = 0; to_list[i] != NULL; i++) {
    for (j = 0; from_list[j] != NULL; j++) {
      cd = iconv_open(to_list[i], from_list[j]);
      if (cd != (iconv_t)-1)
	return cd;
    }
  }
  return (iconv_t)-1;
}

int
sted_looks_like_utf8(const char *s, int len)
{
  const unsigned char *p = (const unsigned char *)s;
  int i = 0, multibyte = 0;

  if (s == NULL || len <= 0)
    return 0;
  while (i < len) {
    unsigned char c = p[i];
    int need;
    if (c <= 0x7f) {
      i++;
      continue;
    }
    if ((c & 0xe0) == 0xc0)
      need = 1;
    else if ((c & 0xf0) == 0xe0)
      need = 2;
    else if ((c & 0xf8) == 0xf0)
      need = 3;
    else
      return 0;
    if (c == 0xc0 || c == 0xc1 || c >= 0xf5)
      return 0;
    i++;
    while (need--) {
      if (i >= len || (p[i] & 0xc0) != 0x80)
	return 0;
      i++;
    }
    multibyte = 1;
  }
  return multibyte;
}

#ifdef __APPLE__
static char *
utf8_nfc(const char *s, char **owned)
{
  CFStringRef cf;
  CFMutableStringRef m;
  CFIndex n;
  char *buf;

  *owned = NULL;
  cf = CFStringCreateWithCString(NULL, s, kCFStringEncodingUTF8);
  if (cf == NULL)
    return (char *)s;
  m = CFStringCreateMutableCopy(NULL, 0, cf);
  CFRelease(cf);
  if (m == NULL)
    return (char *)s;
  CFStringNormalize(m, kCFStringNormalizationFormC);
  n = CFStringGetMaximumSizeForEncoding(CFStringGetLength(m),
					kCFStringEncodingUTF8) + 1;
  buf = (char *)malloc((size_t)n);
  if (buf == NULL ||
      !CFStringGetCString(m, buf, n, kCFStringEncodingUTF8)) {
    free(buf);
    CFRelease(m);
    return (char *)s;
  }
  CFRelease(m);
  *owned = buf;
  return buf;
}
#endif

char *
nkf_convert(char *si, char *so, int maxsize, char *in_mode, char *out_mode)
{
  const char *from, *to;
  char *inptr, *outbuf, *outptr, *owned = NULL, *nfc_owned = NULL;
  char *orig_si;
  size_t inleft, outleft, outcap;
  iconv_t cd;
  char *result;

  if (si == NULL)
    return so != NULL ? so : si;
  if (maxsize == 0)
    return si;

  orig_si = si;
  inleft = strlen(si);
  if (in_mode == NULL && sted_looks_like_utf8(si, (int)inleft))
    from = "UTF-8";
  else
    from = iconv_encoding(in_mode);
  to = iconv_encoding(out_mode);

#ifdef __APPLE__
  if (strcmp(from, "UTF-8") == 0) {
    si = utf8_nfc(si, &nfc_owned);
    inleft = strlen(si);
  }
#endif

  if (maxsize < 0) {
    if (so != NULL)
      outcap = 1024;
    else
      outcap = inleft * 3 + 4;
  } else {
    outcap = (size_t)maxsize;
  }
  if (outcap < 1) {
    free(nfc_owned);
    return si;
  }

  if (so == NULL) {
    owned = (char *)malloc(outcap);
    if (owned == NULL) {
      free(nfc_owned);
      return si;
    }
    outbuf = owned;
  } else {
    outbuf = so;
  }

  if (strcmp(from, to) == 0) {
    size_t n = inleft;
    if (n > outcap - 1)
      n = outcap - 1;
    memmove(outbuf, si, n);
    outbuf[n] = '\0';
    result = so;
    if (owned != NULL) {
      memcpy(orig_si, outbuf, n + 1);
      result = orig_si;
    }
    free(owned);
    free(nfc_owned);
    return result;
  }

  cd = open_cd(to, from);
  if (cd == (iconv_t)-1) {
    size_t n = inleft;
    if (n > outcap - 1)
      n = outcap - 1;
    memmove(outbuf, si, n);
    outbuf[n] = '\0';
    result = so;
    if (owned != NULL) {
      memcpy(orig_si, outbuf, n + 1);
      result = orig_si;
    }
    free(owned);
    free(nfc_owned);
    return result;
  }

  inptr = si;
  outptr = outbuf;
  outleft = outcap - 1;

  while (inleft > 0 && outleft > 0) {
    size_t r = iconv(cd, &inptr, &inleft, &outptr, &outleft);
    if (r != (size_t)-1)
      break;
    if (errno == E2BIG)
      break;
    if (errno == EILSEQ || errno == EINVAL) {
      /* Skip undecodable bytes instead of filling the 36-byte memo with '?'. */
      if (inleft > 0) {
	inptr++;
	inleft--;
      }
      continue;
    }
    break;
  }
  *outptr = '\0';
  iconv_close(cd);

  result = so;
  if (owned != NULL) {
    size_t n = strlen(outbuf);
    memcpy(orig_si, outbuf, n + 1);
    result = orig_si;
  }
  free(owned);
  free(nfc_owned);
  return result;
}

void
sted_ensure_sjis(char *buf, int bufsize)
{
  if (buf == NULL || bufsize < 2)
    return;

#ifdef __APPLE__
  {
    static const CFStringEncoding trials[] = {
      kCFStringEncodingUTF8,
      kCFStringEncodingDOSJapanese,
      kCFStringEncodingShiftJIS,
      kCFStringEncodingMacJapanese,
      kCFStringEncodingEUC_JP,
      0
    };
    CFStringRef cf = NULL;
    CFMutableStringRef m;
    char tmp[4096];
    CFIndex n, used = 0;
    int i, start = 0;

    n = (CFIndex)strlen(buf);
    if (n <= 0)
      return;
    /* Prefer UTF-8 only when the bytes really are UTF-8. Otherwise a
       Shift_JIS name can be misread. */
    if (!sted_looks_like_utf8(buf, (int)n))
      start = 1;
    for (i = start; trials[i] != 0; i++) {
      cf = CFStringCreateWithBytes(NULL, (const UInt8 *)buf, n,
				   trials[i], false);
      if (cf != NULL)
	break;
    }
    if (cf == NULL)
      return;
    m = CFStringCreateMutableCopy(NULL, 0, cf);
    CFRelease(cf);
    if (m == NULL)
      return;
    CFStringNormalize(m, kCFStringNormalizationFormC);
    /* lossByte = 0: drop unmappable chars instead of turning them into '?'. */
    CFStringGetBytes(m, CFRangeMake(0, CFStringGetLength(m)),
		     kCFStringEncodingDOSJapanese, 0, false,
		     (UInt8 *)tmp, sizeof(tmp) - 1, &used);
    CFRelease(m);
    if (used <= 0)
      return;
    tmp[used] = '\0';
    strncpy(buf, tmp, (size_t)bufsize - 1);
    buf[bufsize - 1] = '\0';
    return;
  }
#else
  {
    char tmp[4096];
    if (!sted_looks_like_utf8(buf, (int)strlen(buf)))
      return;
    nkf_convert(buf, tmp, (int)sizeof(tmp), "UTF-8", "SJIS");
    strncpy(buf, tmp, (size_t)bufsize - 1);
    buf[bufsize - 1] = '\0';
  }
#endif
}

char *
nkf_conv(char *si, char *so, char *mode)
{
  return nkf_convert(si, so, so != NULL ? 1024 : -1, NULL, mode);
}
