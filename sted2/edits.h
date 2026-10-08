/*
 * STed: edits.h (edit subroutine header)
 */

#ifndef _STED_EDITS_H_
#define _STED_EDITS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void poplay(int po, int md);
void partf(int a, int ad);
void sttrc(int *po, int *cy, int flg);
int  replace(int po, int ln, int sh, int v);
int  rep_match(int f, int f2, int t, int t2, int a, int m);
int  tai_check(int tr, int ad);
int  chd_check(int tr, int ad, int md);
int  find(int po, int h);
int  find_rep(int po);
void trk_cluc(void);
int  buf_meas(uint8_t *pp, int ln);
int  step_cluc(int ad);
int  step_cluc2(int tr, int top, int len);
int  real_ad(int po, int cy);
int  real_po(int po, int cy);
int  real_cy(int po, int ad);
int  step_no(int ad);
int  step_ad(int ad, int step);
int  sttm_no(int ad);
int  sttm_ad(int ad, int step);
int  padjust(int *po, int i);
int  meas_no(int ad);
int  meas_no2(int ad);
int  meas_add(int me);
int  dat_add(int ln, int me);
int  meas_len(int j);
int  add_set(int tr, int *add);
void undobuf(int a);
void same_shift(int spo, int m, int cu);
int  same_meas_extr(int tr, int ad, int ln);
int  cpybuf_set(int tr, int ad, int ln);
void dat_rev(int tr, int ad, int ln);
int  replicate(int ad, int sh);
int  replicate2(int ad, int sh);
void bendc(int ad, int sh);
int  bendc2(int ad, char *s);
int  bank_no(int j);
int  prog_no(int j);
int  channele_no(int j);
void trk_next(int m);
int  spc_code(char *s, int *ctl);

#ifdef __cplusplus
}
#endif

#endif /* _STED_EDITS_H_ */
