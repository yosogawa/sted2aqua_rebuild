/*
 * STed: key_sub.h (midi in & chord & key name header)
 */

#ifndef _STED_KEY_SUB_H_
#define _STED_KEY_SUB_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

#endif /* _STED_KEY_SUB_H_ */
