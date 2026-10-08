/*
 * STed: track.h (track subroutine header)
 */

#ifndef _STED_TRACK_H_
#define _STED_TRACK_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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
void key_midi_wait(void);
int  ret_midi(int cde, int imd, int ad, int cy);
void cur_down(int *po, int *cy);
void cur_up(int *po, int *cy);
int  ret_chord(int a);
int  mml_inp(int ad);
int  scale_no(int a);
int  comment_inp(int a, int b, int c);
void spcon(int a, int b);
int  same_ext_sub(int tr, int st, int ed, int flg);

int  repeat_ext(uint8_t *obuf, int ln);

int  size_change(int tr, int ln);
void memcpy_l(uint8_t *dest, uint8_t *src, int len);
char *nstr(int a);

void size_ref(void);
int  same_meas_conv(int tr);
int  repeat_comp(int tr);

int  trk_mix_sub(uint8_t *in1, uint8_t *in2);
int  trk_ext_sub(int tr, char *exbuf);
void trk_opt_sub(int tr, int po1, int po2);
int  tai_compres(int tr, int po1, int po2);
void trk_quan(int tr, int po1, int po2);
int  velo_ctrl(int ad, int ln, int vmo);
int  trk_check(int ln);
int  trk_shift(int ad, int ln);
void trk_delete(int ad, int ln);

#ifdef __cplusplus
}
#endif

#endif /* _STED_TRACK_H_ */
