/*
 * STed: assign.h (assign subroutine header)
 */

#ifndef _STED_ASSIGN_H_
#define _STED_ASSIGN_H_

#ifdef __cplusplus
extern "C" {
#endif

void rhy_as(void);
int  tagj(int *po, int fl);
void part_as(void);
void rec_filter(int md);
void filt_init(void);
void stgt_as(void);

#ifdef __cplusplus
}
#endif

#endif /* _STED_ASSIGN_H_ */
