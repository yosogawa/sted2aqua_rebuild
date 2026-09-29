/*
 * STed: edit.h (editor core & editing subroutines header)
 */

#ifndef _STED_EDIT_H_
#define _STED_EDIT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* edit.c */
void trk_ed(void);
void trk_ed_end(int po, int cx, int cy);
void trk_pos_set(int po, int cx, int cy);
void val_add(int ad, int cx, int kc);
void val_sub(int ad, int cx, int kc);
void val_rev(int ad, int cx);
void ret(int ad, int cx, int cy, int kc);
void line_ins(int po, int cy);
void line_ins_aft(int ad);
void keyinp(void);
int  polykey(int cde);
int  ctc(int cur_val, char *inp_str, int key);

/* edits.c */
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
int  buf_meas(unsigned char *pp, int ln);
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

/* redit.c */
void rtrk_ed(int md, int sh);
void rtrk_dis(int p, int ad, int line);
void rhed_dis(int p);
void rhy_step(void);
void rtopat(int me);
void pattor(int me);
void rsttrc(int *no, int *cx, int flg, int pg);

/* trkset.c */
int  trk_sel(int cx);
void trk_no_cur(int i, int m);
void trk_no_dis(void);
void trk_set(int md);
void trmod_set(int tr, int md, int sh);
void trk_lin(int i, int cx, int m);
void ts_swap(int a, int b);
void ts_ins(int a, int b);
void ts_sort(int a, int b);
void ts_copy(int a, int b);
void ts_clear(int a);
void ts_mix(int tr1, int tr2, int tr3);
void ts_measend(int tr1, int sh);
void ts_ext(int i);
void trksize(int tr);
void trksize_c(void);

/* track.c */
void inpmon(void);
void beat_set(void);
int  progmode(char *tmp2);

int  scale_sel(int x, int y, int skey);
void line_del(int po, int cy);
void roll_up(int *po, int *cy);
void roll_down(int *po, int *cy);
void note_ctrl(int po1, int po2);
int  repl_select(int md);

int  size_max(int tr);
int  key_midi_wait(void);
int  ret_midi(int cde, int imd, int ad, int cy);
void cur_down(int *po, int *cy);
void cur_up(int *po, int *cy);
int  ret_chord(int a);
int  mml_inp(int ad);
int  scale_no(int a);
int  comment_inp(int a, int b, int c);
void spcon(int a, int b);
int  same_ext_sub(int tr, int st, int ed, int flg);

int  repeat_ext(unsigned char *obuf, int ln);

int  size_change(int tr, int ln);
void memcpy_l(unsigned char *dest, unsigned char *src, int len);
char *nstr(int a);

void size_ref(void);
int  same_meas_conv(int tr);
int  repeat_comp(int tr);

int  trk_mix_sub(unsigned char *in1, unsigned char *in2);
int  trk_ext_sub(int tr, char *exbuf);
void trk_opt_sub(int tr, int po1, int po2);
int  tai_compres(int tr, int po1, int po2);
void trk_quan(int tr, int po1, int po2);
int  velo_ctrl(int ad, int ln, int vmo);
int  trk_check(int ln);
int  trk_shift(int ad, int ln);
void trk_delete(int ad, int ln);

/* visual.c */
int  vis_edit(int po, int ln, int ssh);
int  vis_bufset(int po, int co, int *para, int *step, int vmo, int bf);
int  b2_set(int ad, int ln);
int  vis_ext_mix(int po, int co, int *para, int *step, int vmo, int ost);
int  vis_a_in(int po, int ln, int *para, int *step, int vmo);
int  vis_a_in_co(int po, int ln, int vmo);
int  vis_a_in_st(int po, int ln);
void vis_a_out(int po, int ln, int *para, int *step, int vmo);
void vis_disp(int co, int cx, int ccx, int *para, int *step, int mo, int c, int vmo);
void vis_wave(int st, int co, int cx, int ccx, int *para, int *step, int md, int vmo);
void bunkatsu(int st, int co, int *da);
int  vis_cut(int co, int st, int *para, int *step);
int  vis_lnk(int co, int *para, int *step);
void vis_zom(int co, int in_step, int out_step, int *step, int *step2);

/* key_sub.c */
void midi_clr(void);
void midi_chout(int a);
void rec_ch_change(int pm);

void key_shift_sub(int no, int *f, int *a);

void str_change(char *s);

char *tim_names(int no);

int  key_shift(int skey, int *f, int *a);

char *prog_name(int ch, int bank, int prog, int h);
char *ctrl_type(int a);

int  key_sub(int code);
int  strch(int och, char *tmp0);

#ifdef __cplusplus
}
#endif

#endif /* _STED_EDIT_H_ */
