/*
 * STed: disp.h (display & drawing subroutines header)
 */

#ifndef _STED_DISP_H_
#define _STED_DISP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* disp.c */
void rev_area(int r_ad, int r_ln, int edit_scr);
void inmd_disp(void);

void path_cut(char *fpath, char *fname);
void path_set(char *path);
void fonread(uint8_t *buf);
void fonload(char *fi);
void fonset(uint8_t *buf);
void sinit(void);

void prn_gspanel(int cx);
void scpanel_dsp(int x, int y, uint8_t *buf);
void scinst_dsp(int x, int y, uint8_t *buf);

char *fstr(int val, int len);
char *spadd(char *s, int c);
char *keystr(int note);
char *keystr1(int note, int key);
char *keystr2(int note, int key);
char *chstr(int ch);
char *scale_str(int key);
char *hex_s(int val, int digits);

int  vinput(int default_val, int mode);
void sinput(char *buf, int mode);
int  inkey2(void);
int  inkey3(void);
void snsclr(void);
void spcut(char *st);
int  knck(const char *st, int p);
int  str_search(const char *tmp0, const char *tmp1);
int  str_last(const char *tmp0);
int  str_val(char *s);
void fnc_dis(int n);
void home2(int y);
void edfield(int m);
char *trk_dis(int y, int ad, int line);
void noed_cls(void);
void noed_cls_t(void);
void cons_md(int x);
void trk_free(int ad);
void trk_no(void);
void sdis(int x, int y, char *s, int c, int c1, int c2);
void sdis2(int x, int y, char *s, int c, int c1, int c2);
void locator_refresh(int force);
void transport_chip_refresh(int force);
void sdis3(int x, int y, char *s, int c, int c1, int c2);
void vdis(int x, int y, int v, int c, int c1, int c2);
void vdis2(int x, int y, int v, int c, int c1, int c2);
void vdis3(int x, int y, int v, int c, int c1, int c2);
void g_print2(int x, int y, char *s, int c1);
void tdis(int x, int y, char *s, int c, int c1);
void msg(char *s);
void msg_clr(void);
int  help(int md);

/* graphic.c */
void tg_copy(int p);

void t_scrw(int x1, int y1, int x2, int y2, int dx, int dy);

void tg_copy2(int edit_scr);

void g_print(int x, int y, char *s, int c);
void tcur(int a, int b, int c, int cs);
void tcury(int a, int b, int c, int cs);
void txerase(int x, int y, int x1, int y1);
void txfill(int x, int y, int x1, int y1, int page, int pat);
void txxline(int x, int y, int x1, int page, int style);
void txyline(int x, int y, int y1, int page, int style);

/* score.c */
void gra(int po, int sh);
void gra_c(int po);
void gra_gakufu(int po, int md);
void gra_special(int po, int md);
int  gra_add(int ad, int md);
int  st_cv(int st);
void put_sharp(int skey, int x, int y);
void dat_info(int po, int sh);

#ifdef __cplusplus
}
#endif

#endif /* _STED_DISP_H_ */
