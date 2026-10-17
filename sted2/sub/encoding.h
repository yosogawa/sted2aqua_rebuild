#ifndef ___ENCODING_H_
#define ___ENCODING_H_

#ifdef __cplusplus
extern "C" {
#endif

char *jstrupr(char *st);

/*
  Character encoding conversion (iconv). RCP/STed buffers are
  Shift_JIS unless in_mode is set. out_mode accepts UTF-8, EUC,
  SJIS, JIS.
*/

char *encoding_convert(char *si, char *so, int maxsize, char *in_mode, char *out_mode);
int sted_looks_like_utf8(const char *s, int len);
void sted_ensure_sjis(char *buf, int bufsize);
/* Host UTF-8 (APFS) filename → SJIS for kanji-ROM / STed text.
   Does not modify the source. SJIS/ASCII names are returned as-is. */
const char *sted_filename_for_font(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* ___ENCODING_H_ */
