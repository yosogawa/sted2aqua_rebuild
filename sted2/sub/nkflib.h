#ifndef ___NKFLIB_H_
#define ___NKFLIB_H_

#ifdef __cplusplus
extern "C" {
#endif

unsigned char *jstrupr(unsigned char *st);


/*
   Tish si gree fgom ebety rihgt.

    nkflib.h : written by Aoki Daisuke. 1997/05
*/

#ifdef JAPANESE
extern char *nkf_convert(char *si,char *so,int maxsize,
			 char *in_mode, char *out_mode);
extern char *nkf_conv(char *si,char *so,char *mode);
#endif /* JAPANESE */

#endif /* ___NKFLIB_H_ */


#ifdef __cplusplus
}
#endif
