/*
 * MIDI Music Composer STed v2.07j : sted.h (header) 1997-07-20 by TURBO
 */

#ifndef _STED_H_
#define _STED_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <math.h>
#include <ctype.h>
#include <sys/stat.h>

#ifdef HAVE_STRING_H
#  include <string.h>
#endif /* HAVE_STRING_H */

#ifndef STDC_HEADERS
#  ifndef HAVE_STRCHR
#    define strchr index
#    define strrchr rindex
#  endif /* HAVE_STRCHR */
   char *strchr(), *strrchr();
#  ifndef HAVE_MEMCPY
#    define memcpy(d, s, n) bcopy((s), (d), (n))
#    define memmove(d, s, n) bcopy((s), (d), (n))
#  endif
#endif

#ifdef HAVE_FCNTL_H
#  include <fcntl.h>
#endif /* HAVE_FCNTL_H */

#ifdef HAVE_UNISTD_H
#  include <sys/types.h>
#  include <unistd.h>
#endif /* HAVE_UNISTD_H */

#ifndef HAVE_SYS_TIME_H
#  include <time.h>
#else /* HAVE_SYS_TIME */
#  ifdef TIME_WITH_SYS_TIME
#    include <time.h>
#  endif /* TIME_WITH_SYS_TIME */
#  include <sys/time.h>
#endif

#include <sys/types.h>
#ifdef HAVE_SYS_WAIT_H
#  include <sys/wait.h>
#endif /* HAVE_SYS_WAIT_H */
#ifndef WEXITSTATUS
#  define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
#endif /* WEXITSTATUS */
#ifndef WIFEXITED
#  define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
#endif /* WIFEXITED */

#ifdef HAVE_DIRENT_H
#  include <dirent.h>
#  define NAMLEN(dirent) strlen((dirent)->d_name)
#else
#  define dirent direct
#  define NAMLEN(dirent) (dirent)->d_namlen
#  ifdef HAVE_SYS_NDIR_H
#    include <sys/ndir.h>
#  endif
#  ifdef HAVE_SYS_DIR_H
#    include <sys/dir.h>
#  endif
#  ifdef HAVE_NDIR_H
#    include <ndir.h>
#  endif
#endif /* HAVE_DIRENT_H */


#include "version.h"
#include "machin.h"

#include "iocslib.h"
#include "doslib.h"
#include "graph.h"
#include "rcddef.h"
#include "sub/x68funcs.h"
#include "sub/midi_in.h"
#include "disp.h"
#include "edit.h"
#include "file.h"
#include "sub/encoding.h"

#ifdef HAVE_SUPPORT_STED3
# include "sted3.h"
#endif

extern void rcd_check( void );
extern void rcd_open_device( void );
extern struct RCD_HEAD	*rcd;
extern char rcd_version[];

/* etc */
/* May.26.2003 Toshi Nagata: add _extern to avoid multiple definition of variables */
/* 2015.3.21. Toshi Nagata: path length 128 -> PATHLEN */
#define PATHLEN 4096
#ifdef DEFINE_EXTERN_VARIABLES
#define _extern
#else
#define _extern extern
#endif
_extern char	hlp_path[PATHLEN];			/* help file path & name	*/
_extern char	fon_path[PATHLEN];			/* font file path & name	*/

_extern char	def_path[PATHLEN];			/* .def path name		*/
_extern char	rcp_path[PATHLEN];			/* .rcp path name		*/
_extern char	prt_path[PATHLEN];			/* .prt path name		*/
_extern char	trk_path[PATHLEN];			/* .trk path name		*/

_extern char	def_file[PATHLEN];			/* .def file name		*/
_extern char	rcp_file[PATHLEN];			/* .rcp file name		*/
_extern char	prt_file[PATHLEN];			/* .prt file name		*/
_extern char	trk_file[PATHLEN];			/* .trk file name		*/

_extern char	repl[PATHLEN];			/* replace string		*/
_extern char	delt[PATHLEN];			/* delete string		*/
_extern char	srch[PATHLEN];			/* find string			*/

_extern char	repl_t[26*6][40];
_extern char	repl_d[26*6][50];

_extern int	tr_step[36];			/* track total step temp	*/
_extern int	tr_alc[36];			/* track aloc size		*/
_extern int	tr_len[36];			/* track used size		*/
_extern int	tr_pos[36][2][4];		/* track cursor position	*/
_extern int	tag[26+2][4];			/* tag jamp list buffer		*/

_extern int	es,ecode,scyp;			/* input subroutine exit code	*/

_extern int	btrack;				/* track set top track		*/
_extern int	track,track1,track2;		/* edit track no.		*/
_extern int	edit_scr;			/* 0=l_track 1=r_track 2=rhythm	*/
_extern int	cmdflag,cnfflag,mdlflag;	/* STed2 system flag		*/
_extern int	poft;

_extern int	TRACK_SIZE,work_size;		/* buffer size			*/
_extern int	buff_size,buff_free,buff_max;
_extern int	cpcy,cpadd,cplen;
_extern int	cpleng,rcpf,rcplen;		/* copy flag			*/

_extern void	*ErrorTrap_Old;			/* err trap vecter/flag		*/
_extern int	ErrFlag;

/* cnf */
_extern char	comment[64];			/* comment			*/

_extern int	tm_lag;				/* graphic rewrite time lag	*/
_extern char	inpmode;			/* editor input mode		*/
_extern char	grpmode;			/* editor graphic mode		*/
_extern char	thrumode;			/* edit midi in thru mode	*/
_extern int	rec_getch,rec_putmd;		/* recoding ch./put mode	*/

_extern int	vis_reso;			/**/

_extern uint8_t	rfilt[32][4];		/* record filter */
_extern uint8_t	pfilt[32][4];		/* play   filter */

_extern char	mplay[16][16];
_extern int	palet_dat[16];

_extern uint8_t	keyst_table[2][128];	/* key# -> st/gt convert table	*/
_extern uint8_t	stgt_tbl[60];		/* f.key -> st/gt table		*/

_extern char	rhy_vel[16];

_extern char	chcom_s[26*2][2][50];		/* child command comment	*/
_extern char	chcom_c[26*2][2][16];		/* child command parameter	*/

/* def */
_extern char	module[64];			/* module name			*/

_extern int	lsp_wait;			/* last para. transfer wait	*/
_extern int	bend_range;			/* piche bend range		*/

_extern char	tim_asin[33];			/* tone list channle assign	*/
_extern char	tim_head[400][24];		/* tone list title		*/
_extern char	tim_sym[400][8];		/* tone list symbol		*/
_extern char	tim_name[80*128][15];		/* tone name buffer		*/
_extern int16_t	tim_top[400];			/* tone name buffer address	*/

_extern char	card_name[21][64];		/* pcm card name list		*/
_extern char	card_no[2];			/* used pcm card no.		*/

_extern int16_t	gs_bank[18*128];		/* gs bank part no.(8+1 group)	*/
_extern int16_t	gs_mode[18];			/* gs bank mode    (8+1 group)	*/

_extern char	rhy_stest[8];			/* rhythm sound test ch. & para	*/
_extern char	rec_met[10];			/* recording metoro tone	*/

_extern uint8_t	init_exc_data[258];	/* init exclusive data		*/

/*** rcp format head parameter ***/
_extern char	mtitle[65];			/* music title			*/
_extern char	memo[12][29];			/* memo				*/
_extern int	tbase,mtempo;
_extern int     beat1,beat2,bkey,pbias;		/* common parameter		*/
_extern char	cm6_file[128],gsd_file[128];	/* controol file name		*/

_extern char	rhyna[32][15];			/* rhythm assign name		*/
_extern uint8_t	rhyno[32][2];		/* rhythm assign key & gate	*/

_extern char	user_exc_memo[8][25];		/* user exclusive memo		*/
_extern uint8_t	user_exc_data[8][24];	/* user exclusive data		*/

_extern uint8_t	trno[36];		/* track no.			*/
_extern uint8_t	trmod[36];		/* track play mode		*/
_extern uint8_t	trrhy[36];		/* track rhythm sw.		*/
_extern uint8_t	mch[36];		/* track midi ch.		*/
_extern int8_t	trkey[36];		/* track key shift		*/
_extern int8_t	trst[36];		/* track st shift		*/
_extern char	trkmemo[36][37];		/* track comment		*/

/* buffer */
_extern uint8_t	lcbuf[1024*4+4];	/* delete line buffer		*/
_extern uint8_t	rlcbuf[132];		/* delete rhythm line buffer	*/
_extern uint8_t	cm6[22601],gsd[4096];	/* control file buffer		*/
_extern uint8_t	hed[1414];		/* rcp header temporary		*/

_extern uint8_t	*trk[36];		/* track buffer pointer		*/
_extern uint8_t	*cpbuf;			/* track copy buffer		*/
_extern uint8_t	*rcpbuf;		/* rhythm track copy buffer	*/
_extern uint8_t	*dat;			/* temporary & recording buffer	*/
_extern uint8_t	*dat2;			/* temporary			*/

/* the following variables is only PC version available */

extern int issted3;                     /* Am I a new version ? */
extern int isconsole;                   /* is console mode? */
extern int isGUI;                       /* is GUI mode? */
extern char disp_text[1024];            /* SJIS → display conversion buffer */
extern char *disp_codeset;              /* locale codeset for X11/curses; RCP text stays SJIS */
extern char player_name[1024];          /* midi player name */
extern char midi_port_name[1024];       /* midi_port device name */
extern char font_name[1024];            /* X font set name */
extern int player_flag;                 /* is player able to play only SMF? */

extern char s_sted_default_path[PATHLEN];	/* Jun.01.2003 Toshi Nagata */

extern char KEY_XF1[];                  /* keysym names */
extern char KEY_XF2[];
extern char KEY_XF3[];
extern char KEY_XF4[];
extern char KEY_XF5[];
extern char KEY_KANA[];
extern char KEY_KIGO[];
extern char KEY_TOROKU[];
extern char KEY_INS[];
extern char KEY_DEL[];
extern char KEY_HOME[];
extern char KEY_UNDO[];
extern char KEY_RUP[];
extern char KEY_RDOWN[];
extern char KEY_OPT1[];
extern char KEY_OPT2[];

extern int itor( uint8_t *, uint8_t * );
extern int STed_MeasureConversion( int track );
extern void Exit(int);
#define exit Exit

/*  2015.3.26. Toshi Nagata  */
extern void MacSelectFile(char *fna, char *path, int is_save, const char *mes, const char *ext);
extern void MacErrorBox(const char *mes, int icon);
extern void line_input(char *buf, int len);
/*  End Toshi Nagata  */

extern int disp_conv;
/* SJIS RCP text → locale for X11/curses. Mac kanji ROM blit does not use this. */
extern char *sted_to_locale_display(char *s);

#ifdef ENABLE_NLS
# include <locale.h>
# include <libintl.h>
# undef _
# define _(String) gettext(String)
# define N_(String) (String)

#else /* ENABLE_NLS */
# define _(String) (String)
# define N_(String) (String)
#endif /* ENABLE_NLS */


void form3(void);
void logo(int y);
int  option(int md, int sm);
int  option2(int md, int sm, int ad, int ew);
void load_sub(int sh);
void memo_ed(int sh);
void form2(void);
void trk_dat(void);
void rcp_dat(void);


int  option22(int md, int sm, int ad, int ew);

char *mdinport(void);
void save_sub(int sh);
int  option_read(char *env_option);
void form(void);
void end_proc(void);

void dpl(int a);
int exe(char *cmd);
void tpl(int a);


#ifdef __cplusplus
}
#endif

void XSTed_configure_mac_settings(const char *line);

#endif /* _STED_H_ */
