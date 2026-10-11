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

#ifdef __cplusplus
}
#endif

#endif /* _STED_EDIT_H_ */
