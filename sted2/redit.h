/*
 * STed: redit.h (rhythm track edit header)
 */

#ifndef _STED_REDIT_H_
#define _STED_REDIT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void rtrk_ed(int md, int sh);
void rtrk_dis(int p, int ad, int line);
void rhed_dis(int p);
void rhy_step(void);
void rtopat(int me);
void pattor(int me);
void rsttrc(int *no, int *cx, int flg, int pg);

#ifdef __cplusplus
}
#endif

#endif /* _STED_REDIT_H_ */
