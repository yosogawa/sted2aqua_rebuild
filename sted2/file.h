/*
 * STed: file.h (file & data processing subroutines header)
 */

#ifndef _STED_FILE_H_
#define _STED_FILE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* file.c */
char *sread(int a, int b);
int  exc_load(char *fna);
int  exc_save(char *fna, int mo);

int  dload(char *fna, int md);
int  dsave(char *fna);
int  part_load(char *fna);
int  part_save(char *fna, int ln);
int  trk_load(char *fna);
int  trk_save(char *fna);
int  part_text_save(char *fna, int ad, int ln);
void uex_read(int po, int no);
void uex_set(int po, int no);
int  rex_load(char *fna, int mo);
int  rex_save(char *fna, int mo);
int  ras_load(char *fna);
int  ras_save(char *fna);
int  timload(char *fna);
int  timsave(char *fna);
int  gsdload(char *fna);
void sset(char *s, int a, int b);
void hedset(int max);
void hedread(void);
void thedset(int tr, int sz);
int  thedread(int tr);
int  bufload(void);
int  bufset(void);
int  bufload_trk(int tr);
int  bufset_trk(int tr);
void dplay(int mo, int tr, int meas);
void cdplay(int tr, int po, int ln, int md);
void cntplay(int tr, int po);
int  last_tempo(int meas);
int  lsp_para_set(int tr, int po, int *me_ad, int wcc);
int  lsp_wait_chack(int meas);
void tim_buf_trns(void);
void tim_var_read(void);
void tim_name_read(void);
void gsd_buf_trns(void);
void gsd_var_read(void);
int  rcp_buf_put(char *ptr, int po, int ln);
int  rcp_buf_get(char *ptr, int po, int ln);
void dclr(void);
void same_cluc(int tr, int ofset);
int  gomi_cut(uint8_t *ptr, int size);
void dinit(void);

/* defload.c */
void rhinit(void);
void rhinit2(int i);
void user_exc_init(void);
void user_exc_init2(int i);
void stgt_init(void);
void stgt_init2(int i);
void asin_change(void);
void asin_init(void);
void cnfinit(void);
void definit(void);
int  defload(char *fi, int md);
void cnf_stgt(char *li, int nn);
void cnf_rvel(char *li);
void cnf_prog(char *li, char *li2, int md);
void cnf_repl(char *li, char *li2, int md);
void def_init(char *li);
int  defsave(char *fi, int md, int mutef, int initf);
int  buf_put(char *ptr, int ln);
int  def_bsave(char *fi, int last_no, int mutef, int initf);
int  def_bload(char *fi, int md);

/* assign.c */
void rhy_as(void);
int  rhy_as_sel(int no);
void rhy_as_para(void);
int  tagj(int *po, int fl);
void part_as(void);
int  part_sel(int yy);
void rec_filter(int md);
void filt_init(void);
void stgt_as(void);

/* cm6con.c */
void cm64_con(void);
void pfil(void);
void keyd(void);
void keyd_inp(int a);
void pch_fld(int m);
void card_sel(void);
void card_name_get(void);
int  sys_mode(void);
int  tone_patch(int mo);
void pat_para_set(int mo, int ad, int i);
void pat_para_dis(int m, int ad);
int  rhy_patch(void);
int  user_tone_list(void);
int  cm6_tonesel(int slmd);
void cminit(void);
void cm6_write(int ad, int ad2, int ln);
void cm6_tone_write(int i);
void cm6_la_write(int i);
void cm6_pcm_write(int i);
void trns_check(void);
void timtra(void);
void all_note_off(int m);
void temporary_set(void);
void twait(int ti);
void gsd_con(void);
void gsinit(void);

/* mkeyb.c */
void m_keyb(int ch, int bank, int prg, int velo);

/* record.c */
int  real_record(void);
void mix_one(int c, int a);
void rec_ch_disp(void);
void rec_ch_change(int pm);
void rec_md_change(void);
void rec_mt_change(int md);
void rec_sy_change(void);
void rec_meas_change(void);
int  countstart(int md, int smeas);
int  get_delta(int p);
void put_delta(int p, int val);
void entry(int d0, int d1, int d2, int d3);
void record_flash(int dt);
void proc_dummy(int dt);
void check(int data);
int  search(int pp);
void ent_dat(int nt, int vl, int gt);
void note_off(int p);
void note_on(int p);
void aft_key(int p);
void ctl_chg(int p);
void prg_chg(int p);
void aft_tch(int p);
void pt_bend(int p);
void run_sts(int p);
int  num_check(int num, int limit);
void proc_excl(uint8_t *text, int length, int code);
void proc_next(void);
void track_end(void);
void rec_conv(int mm);
int  rec_ext(int i);
int  meas_adjust(int i, int topm);
int  meas_adj_sub(int ln, int st);

/* exclu.c */
void excins(uint8_t *da, int co, int nn);
void excdel(uint8_t *da, int co, int nn);
void excsum(uint8_t *da, int nn);
void exc_dis(int no, int cx, int co, int m);
int  dot_ed(int dx, int co);

void exc_view(int ad);

void user_exc_as(int no);
void user_exc_dis(int no);

void dot_ctrl(int m);
void dot_dis(int cx);
void dattopat(void);
void pattodat(void);
void exc_edit(int ad, int cy, int sw);
int  spc_select(void);
/* select.c */
int  file_check(char *fna, int cc);
void file_back(char *fna);

typedef struct {
  char          name[256];
  uint8_t far;
  unsigned int  fln;
  unsigned int  ftm;
  unsigned int  fdt;
} X68_DIR;
int  drv_code(char *pth);
void dir_disp(X68_DIR *dir, int nm, int p, int cx, int sp, int ln);
void memo_load(char *pth, char *fna);
void rcp_dat2(int n, int m);

char *cm6_tone_name(int mo, int i);
void com_sel(void);



int  fdir(char *pth, char *exts, X68_DIR *dir, int w);


void stest_para_disp(int x);
void stest_para_edit(int x, int a);
int  drvchk(char *pth);
int  drvchk2(char *pth);
void path_down(char *pth);
void memo_disp(void);
void frees(char *pth, int cx);

void fsel(char *fna, char *pth, int w);
void s_test(int ch, int bank, int prg, int note, int velo);
int  yn_sel(void);

int  timsel(int bmode, int bank, int gx, int prog);
int  timsel2(int ch, int bank, int gx, int prog);
int  trk_sel_menu(int md);
int  varia(char *pname, int bank, int prg, int md);

#ifdef __cplusplus
}
#endif

#endif /* _STED_FILE_H_ */
