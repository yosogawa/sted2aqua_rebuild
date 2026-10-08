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
