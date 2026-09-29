#include <stdint.h>
/*--------------------------------------------------------------*/
/* rcddef.h							*/
/*--------------------------------------------------------------*/
/* RCD ¥É¥é¥¤¥Ð¡¦¥¢¥¯¥»¥¹ÍÑ¹½Â¤ÂÎÄêµÁ   RCD v3.01 °Ê¹ßÍÑ	*/
/*--------------------------------------------------------------*/
/*								*/
/* RCD ¤òÍøÍÑ¤¹¤ë¥×¥í¥°¥é¥à¤Ï¡¢					*/
/* 								*/
/* ËÜ¥Õ¥¡¥¤¥ë¤ò¥¤¥ó¥¯¥ë¡¼¥É¤·¤Æ²¼¤µ¤¤¡£				*/
/* 								*/
/* extern struct RCD_HEAD *rcd;   <--- ¥Ý¥¤¥ó¥¿¤Î³°ÉôÄêµÁ¤ò¤·¡¢	*/
/* 								*/
/* rcdcheck ¤ò¥³¡¼¥ë¤·¤Æ¹½Â¤ÂÎ¤Î¥¢¥É¥ì¥¹¤òµá¤á¤¿¸å¤Ë¡¢		*/
/* 								*/
/* rcd->fmt ¤Ê¤É¤Î¤è¤¦¤Ë¥¢¥¯¥»¥¹¤¹¤ë¡£				*/
/* 								*/
/* ¥ë¡¼¥Á¥ó¥³¡¼¥ë¤Ï¡¢(*rcd->begin)() ¤Î¤è¤¦¤Ë¤¹¤ë¡£		*/
/*								*/
/*--------------------------------------------------------------*/

#define	TRK_NUM	36			/* ¥È¥é¥Ã¥¯¿ô		*/
#define	CHL_NUM	34 			/* ¥Á¥ã¥Í¥ë¿ô		*/
					/*  32/33 ¤ÏÌ¤»ÈÍÑ	*/

struct RCD_HEAD {
	char	title[4];		/* "RCD "		*/
	char	version[4];		/* "N.NN"		*/
	int	staymark;

	char	data_valid;		/* RCP/MCP Í­¸ú»þ 1	*/
	char	tone_valid;		/* CM6/MTD Í­¸ú»þ 1	*/
	char	word_valid;		/* WRD Í­¸ú»þ 1		*/
	char	fmt;			/* MCP:0 RCP:1 R36:2	*/

	char	*data_adr;		/* MCP/RCP address	*/
	char	*tone_adr;		/* MTD/CM6 address	*/
	char	*word_adr;		/* WRD address		*/
	char	*gsd_adr;		/* GSD address		*/ /*(v2.92)*/

	void	(*init)( void );	/* ¥Ñ¥é¥á¡¼¥¿½é´ü²½	*/ /* void (v2.70)*/
	void	(*setup)( void );	/* ²»¿§¥Ç¡¼¥¿½ñ¤­¹þ¤ß	*/ /* void (v2.70)*/
	void	(*begin)( void );	/* ±éÁÕ³«»Ï		*/ /* void (v2.70)*/
	void	(*end)( void );		/* ±éÁÕ½ªÎ»		*/ /* void (v2.70)*/

	void	(*md_put)( char );	/* MIDI 1 byte ½ÐÎÏ	*/ /* void (v2.70)*/
	int	(*md_get)( void );	/* MIDI 1 byte ÆþÎÏ¡¡(-1)ÆþÎÏ¤Ê¤·*/ /*(v2.70)*/
	void	(*md_put2)( char );	/* MIDI 1 byte ½ÐÎÏ(dual)*/ /* void (v2.92)*/
	void	(*mix_out)( char * );	/* MIX OUT(buff adrs)end=$ff*/ /*(v3.01)*/

	int	act;			/* 1:±éÁÕÃæ		*/
	int	sts;			/* 0:RUN 1:STOP 2:SEARCH 3:CUE	*/ /*(v2.70)*/
	int	tar_trk;		/* TARGET TRACK		*/
	int	tar_bar;		/* TARGET BAR		*/

	char	dummy1[ 18 ];

	int	tempo;			/* ¸½ºß¤Î¥Æ¥ó¥Ý		*/
	int	basetempo;		/* ¸µ¤Î¥Æ¥ó¥Ý		*/

	int	totalcount;		/* ²»Éä¥Õ¥¡¥¤¥ëÄ¹	*/
	char	filename[30];		/* ²»Éä¥Õ¥¡¥¤¥ëÌ¾	*/
	char	tonename[30];		/* ²»¿§¥Õ¥¡¥¤¥ëÌ¾	*/

	char	dummy2[ 72 ];

	int	bufcap;			/* ¥Ð¥Ã¥Õ¥¡ÍÆÎÌ		*/  /*(v2.70)*/

	int	MIDI_avl;		/* MIDI ½ÐÎÏÍ­¸ú(trk.no)*/  /*(v2.80)*/
	unsigned char MIDI_req[16];	/* MIDI ½ÐÎÏÍ×µá¥Ð¥Ã¥Õ¥¡*/  /*(v2.80)*/
		/* (80) (ch) ...	:Á¬ÈÙÁªÝ¼*/
		/* (90) (ke) (ve)	:É°Äµ	*/  /*(v2.92)*/
		/* (b0) (xx) (xx)	:ºÝÄÛ°ÙÁªÝ¼*/
		/* (c0) (xx) ...	:ÌßÛ¸Þ×ÑÁªÝ¼Þ	*/
		/* (e0) (xx) (xx)	:Ëß¯ÁÍÞÝÀÞ°	*/
		/* (f0) (hh) (mm) (ll) (xx) :ÒÓØ×²Ä	*/
		/* (f1) (hh) (mm) (ll) (xx) (id):ÒÓØ×²Ä	*/  /*(v2.92)*/

	char	LA_VOL;			/* LA part master vol   */ /*(v2.80)*/
	char	LA_RVB_Mode;		/* LA part reverb Mode  */ /*(v2.80)*/
	char	LA_RVB_Time;		/* LA part reverb Time  */ /*(v2.80)*/
	char	LA_RVB_Level;		/* LA part reverb Level */ /*(v2.80)*/

	char	PCM_VOL;		/* PCM part master vol  */ /*(v2.80)*/
	char	PCM_RVB_Mode;		/* PCM part reverb Mode */ /*(v2.80)*/
	char	PCM_RVB_Time;		/* PCM part reverb Time */ /*(v2.80)*/
	char	PCM_RVB_Level;		/* PCM part reverb Level*/ /*(v2.80)*/

	int	filter_mode;		/* 0:Ìµ¸ú 1:PRG¤Î¤ß 2:LA/PCM¤¢¤ê */ /*(v2.90)*/
	char	*filter_data;		/* Filter Data¥¢¥É¥ì¥¹	*/ /*(v2.90)*/

	int	play_mode;		/* 0:normal 1:slow 2:fast 3:slow2 4:fast2 */ /*(v2.92)*/
	int	mute_mode;		/* 0:off 1:cm64 2:sc55	*/ /*(v2.92)*/
	int	init_mode;		/* 0:off 1:cm64 2:sc55 3:cm+sc */ /*(v2.92)*/
	char	scan_mode;		/* key scan flag 0:off 1:on */
	char	rsmd_mode;		/* midi port flag 0:midi 1:midi+rs/sb 2:rs/sb */
	int16_t	fade_time;		/* fade out speed	*/
	char	fade_count;		/* fade out start flag 128:start 0:end */
	char	moduletype;		/* panel display 0:cm64 1:sc55 2:cm+sc */
	char	fade_mode;		/* fade out mode 0:exclusive a:volume 2:expres*/
	char	timer_mode;		/* RS-MIDI timer mode 0:OPM Timer-A 1:OPM Timer-B */
	char	midi_clock;		/* MIDI clock out 0:disable 1:enable */
	char	put_mode;		/* md_put port mode 0:normal 1:midi 2:rs */
	char	rcd_type;		/* 0:midi+rs-232c 1:midi+polyphon 3:polyphon only */
	char	sc55_fix;		/* 0:off 1:sc55 capi.down emulate */ /*(v3.01)*/

	char	dummyA[19];		/* ³ÈÄ¥ÍÑ¥ê¥¶¡¼¥Ö	*/

	char	mt32_mode;		/* mt-32 mode		*/
	int	exc_wait;		/* exclusive send wait	*/
	char	tim_all;		/* timbre trans mode	*/

	char	gsd_valid;		/* GSD Í­¸ú»þ 1		*/  /*(v2.92)*/
	char	gsdname[30];		/* GSD¥Õ¥¡¥¤¥ëÌ¾	*/

	int	wordcap;		/* word ¥Ð¥Ã¥Õ¥¡ÍÆÎÌ	*/  /*(v2.92)*/

	char	dummyC[10];		/* ³ÈÄ¥ÍÑ¥ê¥¶¡¼¥Ö	*/

	char	GS_VOL;			/* GS part master vol   */ /*(v2.93)*/
	char	GS_PAN;			/* GS part master pan   */ /*(v2.93)*/

	char	GS_RVB_Macro;		/* ¥ê¥Ð¡¼¥Ö¥Þ¥¯¥í	*/ /*(v2.93)*/
	char	GS_RVB_Char;		/* ¥ê¥Ð¡¼¥Ö¡¦¥­¥ã¥é¥¯¥¿¡¼¡¦¥³¥ó¥È¥í¡¼¥ë*/
	char	GS_RVB_Prelpf;		/* ¥ê¥Ð¡¼¥Ö¡¦£Ð£Ò£Å¡¦£Ì£Ð£Æ¡¦¥³¥ó¥È¥í¡¼¥ë*/
	char	GS_RVB_Level;		/* ¥ê¥Ð¡¼¥Ö¡¦¥ì¥Ù¥ë¡¦¥³¥ó¥È¥í¡¼¥ë*/
	char	GS_RVB_Time;		/* ¥ê¥Ð¡¼¥Ö¡¦¥¿¥¤¥à¡¦¥³¥ó¥È¥í¡¼¥ë*/
	char	GS_RVB_Delay;		/* ¥ê¥Ð¡¼¥Ö¡¦DELAY¡¦FEEDBACK¡¦¥³¥ó¥È¥í¡¼¥ë*/
	char	GS_RVB_Send;		/* REVERB SEND LEVEL TO CHOURUS */
	char	GS_RVB_PreDelay;	/* REVERB PREDLY T. */

	char	GS_CHO_Macro;		/* ¥³¡¼¥é¥¹¥Þ¥¯¥í	*/
	char	GS_CHO_Prelpf;		/* ¥³¡¼¥é¥¹¡¦£Ð£Ò£Å¡¦£Ì£Ð£Æ¡¦¥³¥ó¥È¥í¡¼¥ë*/
	char	GS_CHO_Level;		/* ¥³¡¼¥é¥¹¡¦¥ì¥Ù¥ë¡¦¥³¥ó¥È¥í¡¼¥ë*/
	char	GS_CHO_Feed;		/* ¥³¡¼¥é¥¹¡¦¥Õ¥£¡¼¥É¡¦¥Ð¥Ã¥¯*/
	char	GS_CHO_Delay;		/* ¥³¡¼¥é¥¹¡¦£Ä£å£ì£á£ù¡¦¥³¥ó¥È¥í¡¼¥ë*/
	char	GS_CHO_Rate;		/* ¥³¡¼¥é¥¹¡¦£Ò£á£ô£å¡¦¥³¥ó¥È¥í¡¼¥ë*/
	char	GS_CHO_Depth;		/* ¥³¡¼¥é¥¹¡¦£Ä£å£ð£ô£è¡¦¥³¥ó¥È¥í¡¼¥ë*/
	char	GS_CHO_Send;		/* CHORUS SEND LEVEL TO REVERB */
	char	GS_CHO_Send_Dly;

	char	GS_DLY_Macro;		/* ¥Ç¥£¥ì¥¤¥Þ¥¯¥í	*/ /*(v3.01)*/
	char	GS_DLY_Prelpf;
	char	GS_DLY_Time_C;
	char	GS_DLY_Time_L;
	char	GS_DLY_Time_R;
	char	GS_DLY_Lev_C;
	char	GS_DLY_Lev_L;
	char	GS_DLY_Lev_R;
	char	GS_DLY_Level;
	char	GS_DLY_Feed;
	char	GS_DLY_Send_Rev;

	char	GS_EQ_Low_Freq;
	char	GS_EQ_Low_Gain;
	char	GS_EQ_High_Freq;
	char	GS_EQ_High_Gain;

	char	dummyD[32-16];		/* ³ÈÄ¥ÍÑ¥ê¥¶¡¼¥Ö	*/

	char	active[ TRK_NUM ];	/* ¥È¥é¥Ã¥¯Í­¸ú		*/
	char	trk_mask[ TRK_NUM ];	/* TRACK MASK		*/
	char	midich[ TRK_NUM ];	/* MIDI CH		*/

	int	noteptr;		/* É°Ä×ÝÆÝ¸ÞÎß²ÝÀ	*/
	unsigned char	*note_adr;	/* É°Ä×ÝÆÝ¸ÞÊÞ¯Ì§±ÄÞÚ*/
	char	*top[ TRK_NUM ];	/* ¥È¥é¥Ã¥¯¥Ç¡¼¥¿ÀèÆ¬	*/
	unsigned char *ptr[ TRK_NUM ];	/* ¶ÚÝÄÄ×¯¸Îß²Ý*/

	char	flg_vel[ TRK_NUM ];	/* VELOCITY ON Ì×¸Þ	*/
	char	flg_off[ TRK_NUM ];	/* VELOCITY OFF Ì×¸Þ	*/
	char	flg_act;		/* ACTIVE OFF Ì×¸Þ	*/
	char	flg_bar;		/* BAR ÊÑ¹¹ Ì×¸Þ	*/
	char	flg_step;		/* STEP ÊÑ¹¹ Ì×¸Þ	*/
	char	flg_pbend;		/* PITCH BEND ÊÑ¹¹ Ì×¸Þ	*/
	char	flg_vol;		/* VOLUME ÊÑ¹¹ Ì×¸Þ	*/
	char	flg_prg;		/* PROGRAM ÊÑ¹¹¡¡Ì×¸Þ	*/
	char	flg_panpot;		/* PANPOT ÊÑ¹¹ Ì×¸Þ	*/
	char	flg_midich;		/* MIDI CH ÊÑ¹¹ Ì×¸Þ	*/

	char	flg_song;		/* SONG ÃÞ°À Ì×¸Þ	*/
	char	flg_system;		/* SYSTEM ´ØXÌ×¸Þ	*/ /*(v2.80)*/

	char	flg_expres;		/* EXPRESSION ÊÑ¹¹ Ì×¸Þ	*/
	char	flg_modula;		/* MODULATION ÊÑ¹¹ Ì×¸Þ	*/
	char	flg_bank;		/* BANK ÊÑ¹¹ Ì×¸Þ	*/
	char	flg_replay;		/* REPLAY Ì×¸Þ		*/

	char	flg_gssys;		/* GS SYSTEM ´ØXÌ×¸Þ*/ /*(v2.93)*/
	char	flg_gsrev;		/* GSREV ÊÑ¹¹ Ì×¸Þ	*/ /*(v2.93)*/
	char	flg_gscho;		/* GSCHO ÊÑ¹¹ Ì×¸Þ	*/ /*(v2.93)*/
	char	flg_gsinfo;		/* GS info ÊÑ¹¹ Ì×¸Þ	*/ /*(v2.93l)*/
	char	flg_gsinst;		/* GS inst ÊÑ¹¹ Ì×¸Þ	*/ /*(v2.93l)*/
	char	flg_gspanel;		/* GS panel ÊÑ¹¹ Ì×¸Þ	*/ /*(v2.93l)*/

	char	flg_hold1;		/* HOLD1 ÊÑ¹¹ Ì×¸Þ	*/
	char	flg_gsdly;		/* GSDLY ÊÑ¹¹ Ì×¸Þ	*/ /*(v3.00q)*/
	char	flg_bankl;		/* BANK LÊÑ¹¹ Ì×¸Þ	*/ /*(v3.01n)*/

	char	dummyE[9];		/* ³ÈÄ¥ÍÑ¥ê¥¶¡¼¥Ö	*/

	int	panel_tempo;		/* ¥Ñ¥Í¥ë¾å¤Î¥Æ¥ó¥ÝÃÍ	*/
	int	bar[ TRK_NUM ];		/* ¾®ÀáÈÖ¹æ		*/
	int	step[ TRK_NUM ];	/* ¥¹¥Æ¥Ã¥×ÈÖ¹æ		*/
	char	vel[ TRK_NUM ];		/* ¥Ù¥í¥·¥Æ¥£ÃÍ		*/

	int	stepcount;		/* ±éÁÕ³«»Ï»þ¤«¤é¤ÎSTEP COUNT	*/ /*(v2.92)*/
	int16_t	loopcount;		/* 255/256²ó¥ê¥Ô¡¼¥È¤ÎCOUNT	*/ /*(v3.00f)*/

	char	dummyF[12-2];		/* ³ÈÄ¥ÍÑ¥ê¥¶¡¼¥Ö	*/

	char	song[20];		/* SONG¥Ç¡¼¥¿(¥³¥á¥ó¥È)	*/
	char	dummyG[60];		/* ³ÈÄ¥ÍÑ¥ê¥¶¡¼¥Ö	*/

	char	gs_info[18];		/* gs patch name	*/ /*v2.93l*/
	char	gs_inst[34];		/* gs comment		*/ /*v2.93l*/
	char	gs_panel[64];		/* gs panel		*/ /*v2.93l*/

	char	ch_port[ CHL_NUM ];	/* Á¬ÈÙËè¤Îi.f.¼ïÎà 1:MIDI 2:RS-232C/POLYPHON */
	char	ch_part[ CHL_NUM ];	/* Á¬ÈÙËè¤Î²»¸»¼ïÎà 0:LA 1:PCM 2:Â¾(RHYTHM) */
	int	ch_pbend[ CHL_NUM ];	/* Á¬ÈÙËè¤ÎPITCH BENDÃÍ	*/
	char	ch_vol[ CHL_NUM ];	/* Á¬ÈÙËè¤ÎVOLUMEÃÍ	*/
	char	ch_panpot[ CHL_NUM ];	/* Á¬ÈÙËè¤ÎPANPOTÃÍ	*/
	char	flg_ch_prg[ CHL_NUM ];	/* Á¬ÈÙËè¤ÎPROG.CHG¥Õ¥é¥°*/ /*v2.93j*/
	unsigned char ch_prg[ CHL_NUM ];/* Á¬ÈÙËè¤ÎPROGRAMÈÖ¹æ	*/
	char	ch_reverb[ CHL_NUM ];	/* ¥ê¥Ð¡¼¥Ö OFF/ON	*/ /*(v2.80)*/
	char	ch_expr[ CHL_NUM ];	/* Á¬ÈÙËè¤ÎEXPRSSIONÃÍ	*/ /*v2.92*/
	char	ch_modu[ CHL_NUM ];	/* Á¬ÈÙËè¤ÎMODULATIONÃÍ	*/ /*v2.92*/
	char	ch_bank[ CHL_NUM ];	/* Á¬ÈÙËè¤ÎBANKÃÍ	*/ /*v2.92*/
	char	ch_gsrev[ CHL_NUM ];	/* Á¬ÈÙËè¤ÎGS REVERBÃÍ	*/ /*v2.93*/
	char	ch_gscho[ CHL_NUM ];	/* Á¬ÈÙËè¤ÎGS CHORUSÃÍ	*/ /*v2.93*/
	char	ch_hold1[ CHL_NUM ];	/* Á¬ÈÙËè¤ÎHOLD1ÃÍ	*/ /*v2.93*/
	char	ch_gsdly[ CHL_NUM ];	/* Á¬ÈÙËè¤ÎGS DELAYÃÍ	*/ /*v3.00q*/
	char	ch_bankl[ CHL_NUM ];	/* Á¬ÈÙËè¤ÎGS BANK L.ÃÍ	*/ /*v3.01n*/

	char	dummyH[34*1];		/* ³ÈÄ¥ÍÑ¥ê¥¶¡¼¥Ö	*/
};
