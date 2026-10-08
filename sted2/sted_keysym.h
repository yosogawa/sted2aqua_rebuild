/*
 * STed: sted_keysym.h (X11 keysym name buffers)
 *
 * Storage is in sub/xwin_key.c.  Config keys (sted2.cnf):
 *   #KEY_XF1= ... #KEY_XF5=  #KEY_KANA=  #KEY_KIGO=  #KEY_TOROKU=
 *   #KEY_INS=  #KEY_DEL=  #KEY_HOME=  #KEY_UNDO=
 *   #KEY_RUP=  #KEY_RDOWN=  #KEY_OPT1=  #KEY_OPT2=
 */

#ifndef _STED_KEYSYM_H_
#define _STED_KEYSYM_H_

#ifdef __cplusplus
extern "C" {
#endif

#define KEYSYM_NAME_LEN 128

extern char KEY_XF1[KEYSYM_NAME_LEN];
extern char KEY_XF2[KEYSYM_NAME_LEN];
extern char KEY_XF3[KEYSYM_NAME_LEN];
extern char KEY_XF4[KEYSYM_NAME_LEN];
extern char KEY_XF5[KEYSYM_NAME_LEN];
extern char KEY_KANA[KEYSYM_NAME_LEN];
extern char KEY_KIGO[KEYSYM_NAME_LEN];
extern char KEY_TOROKU[KEYSYM_NAME_LEN];
extern char KEY_INS[KEYSYM_NAME_LEN];
extern char KEY_DEL[KEYSYM_NAME_LEN];
extern char KEY_HOME[KEYSYM_NAME_LEN];
extern char KEY_UNDO[KEYSYM_NAME_LEN];
extern char KEY_RUP[KEYSYM_NAME_LEN];
extern char KEY_RDOWN[KEYSYM_NAME_LEN];
extern char KEY_OPT1[KEYSYM_NAME_LEN];
extern char KEY_OPT2[KEYSYM_NAME_LEN];

#ifdef __cplusplus
}
#endif

#endif /* _STED_KEYSYM_H_ */
