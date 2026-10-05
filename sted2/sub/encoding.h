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

extern char *encoding_convert(char *si, char *so, int maxsize,
			      char *in_mode, char *out_mode);
extern int sted_looks_like_utf8(const char *s, int len);
extern void sted_ensure_sjis(char *buf, int bufsize);

#ifdef __cplusplus
}
#endif

#endif /* ___ENCODING_H_ */
