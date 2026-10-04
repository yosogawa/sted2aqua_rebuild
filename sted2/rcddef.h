#ifndef _RCDDEF_H_
#define _RCDDEF_H_

/*--------------------------------------------------------------*/
/* rcddef.h							*/
/*--------------------------------------------------------------*/
/* RCD hCoEANZXp\̄`   RCD v3.01 ǃ~p	*/
/*--------------------------------------------------------------*/
/*								*/
/* RCD ǃpvÓA					*/
/* 								*/
/* {t@CCN[ĥB				*/
/* 								*/
/* extern struct RCD_HEAD *rcd;   <--- |ĈO`A	*/
/* 								*/
/* rcdcheck R[ǃ\̂AhXǃA		*/
/* 								*/
/* rcd->fmt ǃɂ̂͂ANZXB				*/
/* 								*/
/* [`R[́A(*rcd->begin)() ̂͂B		*/
/*								*/
/*--------------------------------------------------------------*/

#define	TRK_NUM	36			/* gbN		*/
#define	CHL_NUM	34 			/* `l		*/
					/*  32/33 ̖gp	*/

#define DATA_ADR_SIZE 512 *1024          /* KB */
#define TONE_ADR_SIZE 64  *1024
#define WORD_ADR_SIZE 64  *1024
#define GSD_ADR_SIZE  64  *1024
#define SMF_ADR_SIZE  512 *1024          /* Dec.18.1998 Daisuke Nagano */


struct RCD_HEAD {
  char	title[4];		/* "RCD "		*/
  char	version[4];		/* "N.NN"		*/
  int	staymark;

  uint8_t	data_valid;		/* RCP/MCP L 1	*/
  uint8_t	tone_valid;		/* CM6/MTD L 1	*/
  uint8_t	word_valid;		/* WRD L 1		*/
  uint8_t	fmt;			/* MCP:0 RCP:1 R36:2	*/

  uint8_t	*data_adr;		/* MCP/RCP address	*/
  uint8_t	*tone_adr;		/* MTD/CM6 address	*/
  uint8_t	*word_adr;		/* WRD address		*/
  uint8_t	*gsd_adr;		/* GSD address		*/ /*(v2.92)*/
  uint8_t	*smf_adr;		/* SMF address		*/ /* Dec.11.1998*/

  void	(*init)( void );	/* p[^	*/ /* void (v2.70)*/
  void	(*setup)( void );	/* Ff[^	*/ /* void (v2.70)*/
  void	(*begin)( void );	/* tJn		*/ /* void (v2.70)*/
  void	(*end)( void );		/* tI		*/ /* void (v2.70)*/

  void	(*md_put)( uint8_t );	/* MIDI 1 byte o	*/ /* void (v2.70)*/
  int	(*md_get)( void );	/* MIDI 1 byte ̂@(-1)̂*/ /*(v2.70)*/
  void	(*md_put2)( uint8_t );	/* MIDI 1 byte o(dual)*/ /* void (v2.92)*/
  void	(*mix_out)( uint8_t * );	/* MIX OUT(buff adrs)end=$ff*/ /*(v3.01)*/

  int	act;			/* 1:t		*/
  int	sts;			/* 0:RUN 1:STOP 2:SEARCH 3:CUE	*/ /*(v2.70)*/
  int	tar_trk;		/* TARGET TRACK		*/
  int	tar_bar;		/* TARGET BAR		*/

  char	dummy1[ 18 ];

  int	tempo;			/* ǃ̂e|		*/
  int	basetempo;		/* ̂e|		*/

  int	totalcount;		/* t@C	*/
  char	filename[30];		/* t@C	*/
  char	tonename[30];		/* Ft@C	*/

  char	dummy2[ 72 ];

  int	bufcap;			/* obt@e		*/  /*(v2.70)*/

  int	MIDI_avl;		/* MIDI o̖L(trk.no)*/  /*(v2.80)*/
  uint8_t MIDI_req[16];	/* MIDI o̗vobt@*/  /*(v2.80)*/
	  /* (80) (ch) ...	:`lǃFW	*/
	  /* (90) (ke) (ve)	:m[gI		*/  /*(v2.92)*/
	  /* (b0) (xx) (xx)	:Rg[ǃFW	*/
	  /* (c0) (xx) ...	:vOǃFW	*/
	  /* (e0) (xx) (xx)	:sb`x_		*/
	  /* (f0) (hh) (mm) (ll) (xx) :Cg	*/
	  /* (f1) (hh) (mm) (ll) (xx) (id):Cg	*/  /*(v2.92)*/

#if 1
  uint8_t	LA_VOL;			/* LA part master vol   */ /*(v2.80)*/
  uint8_t	LA_RVB_Mode;		/* LA part reverb Mode  */ /*(v2.80)*/
  uint8_t	LA_RVB_Time;		/* LA part reverb Time  */ /*(v2.80)*/
  uint8_t	LA_RVB_Level;		/* LA part reverb Level */ /*(v2.80)*/

  uint8_t	PCM_VOL;		/* PCM part master vol  */ /*(v2.80)*/
  uint8_t	PCM_RVB_Mode;		/* PCM part reverb Mode */ /*(v2.80)*/
  uint8_t	PCM_RVB_Time;		/* PCM part reverb Time */ /*(v2.80)*/
  uint8_t	PCM_RVB_Level;		/* PCM part reverb Level*/ /*(v2.80)*/

  int	filter_mode;		/* 0: 1:PRĜ 2:LA/PCM */ /*(v2.90)*/
  uint8_t	*filter_data;		/* Filter DataAhX	*/ /*(v2.90)*/

  int	play_mode;	/* 0:normal 1:slow 2:fast 3:slow2 4:fast2 */ /*(v2.92)*/
#endif
  int	mute_mode;	/* 0:off 1:cm64 2:sc55	*/ /*(v2.92)*/
  int	init_mode;	/* 0:off 1:cm64 2:sc55 3:cm+sc */ /*(v2.92)*/
#if 1
  uint8_t	scan_mode;	/* key scan flag 0:off 1:on */
  uint8_t	rsmd_mode;	/* midi port flag 0:midi 1:midi+rs/sb 2:rs/sb */
  int16_t	fade_time;	/* fade out speed	*/
  uint8_t	fade_count;	/* fade out start flag 128:start 0:end */
#endif
  uint8_t	moduletype;	/* panel display 0:cm64 1:sc55 2:cm+sc */
#if 1
  uint8_t	fade_mode;	/* fade out mode 0:exclusive a:volume 2:expres*/
  uint8_t	timer_mode;	/* RS-MIDI timer mode 0:OPM Timer-A 1:OPM Timer-B */
  uint8_t	midi_clock;	/* MIDI clock out 0:disable 1:enable */
#endif
  uint8_t	put_mode;	/* md_put port mode 0:normal 1:midi 2:rs */
#if 1
  uint8_t	rcd_type;	/* 0:midi+rs-232c 1:midi+polyphon 3:polyphon only */
  uint8_t	sc55_fix;	/* 0:off 1:sc55 capi.down emulate */ /*(v3.01)*/

  char	dummyA[19];		/* gpU[u	*/

  uint8_t	mt32_mode;		/* mt-32 mode		*/
  int	exc_wait;		/* exclusive send wait	*/
  uint8_t	tim_all;		/* timbre trans mode	*/
#endif

  uint8_t	gsd_valid;		/* GSD L 1		*/  /*(v2.92)*/
  char	gsdname[30];		/* GSDt@C	*/

  int	wordcap;		/* word obt@e	*/  /*(v2.92)*/

#if 1
  char	dummyC[10];		/* gpU[u	*/

  uint8_t	GS_VOL;			/* GS part master vol   */ /*(v2.93)*/
  uint8_t	GS_PAN;			/* GS part master pan   */ /*(v2.93)*/

  uint8_t	GS_RVB_Macro;		/* o[u}N	*/ /*(v2.93)*/
  uint8_t	GS_RVB_Char;		/* o[uELN^[ERg[*/
  uint8_t	GS_RVB_Prelpf;		/* o[uEoqdEkoeERg[*/
  uint8_t	GS_RVB_Level;		/* o[uExERg[*/
  uint8_t	GS_RVB_Time;		/* o[uE^CERg[*/
  uint8_t	GS_RVB_Delay;		/* o[uEDELAYEFEEDBACKERg[*/
  uint8_t	GS_RVB_Send;		/* REVERB SEND LEVEL TO CHOURUS */
  uint8_t	GS_RVB_PreDelay;	/* REVERB PREDLY T. */

  uint8_t	GS_CHO_Macro;		/* R[X}N	*/
  uint8_t	GS_CHO_Prelpf;		/* R[XEoqdEkoeERg[*/
  uint8_t	GS_CHO_Level;		/* R[XExERg[*/
  uint8_t	GS_CHO_Feed;		/* R[XEtB[hEobN*/
  uint8_t	GS_CHO_Delay;		/* R[XEcERg[*/
  uint8_t	GS_CHO_Rate;		/* R[XEqERg[*/
  char	GS_CHO_Depth;		/* R[XEcERg[*/
  uint8_t	GS_CHO_Send;		/* CHORUS SEND LEVEL TO REVERB */
  uint8_t	GS_CHO_Send_Dly;

  uint8_t	GS_DLY_Macro;		/* fBC}N	*/ /*(v3.01)*/
  uint8_t	GS_DLY_Prelpf;
  uint8_t	GS_DLY_Time_C;
  uint8_t	GS_DLY_Time_L;
  uint8_t	GS_DLY_Time_R;
  uint8_t	GS_DLY_Lev_C;
  uint8_t	GS_DLY_Lev_L;
  uint8_t	GS_DLY_Lev_R;
  uint8_t	GS_DLY_Level;
  uint8_t	GS_DLY_Feed;
  uint8_t	GS_DLY_Send_Rev;

  uint8_t	GS_EQ_Low_Freq;
  uint8_t	GS_EQ_Low_Gain;
  uint8_t	GS_EQ_High_Freq;
  uint8_t	GS_EQ_High_Gain;

  char	dummyD[32-16];		/* gpU[u	*/
#endif

  char	active[ TRK_NUM ];	/* gbNL		*/
  char	trk_mask[ TRK_NUM ];	/* TRACK MASK		*/
#if 1
  char	midich[ TRK_NUM ];	/* MIDI CH		*/

  int	noteptr;		/* m[gjO|C^	*/
  uint8_t	*note_adr;	/* m[gjOobt@AhX	*/
  char	*top[ TRK_NUM ];	/* gbNf[^̂	*/
  uint8_t *ptr[ TRK_NUM ];	/* JggbN|C^	*/

  char	flg_vel[ TRK_NUM ];	/* VELOCITY ON tO	*/
  char	flg_off[ TRK_NUM ];	/* VELOCITY OFF tO	*/
  char	flg_act;		/* ACTIVE OFF tO	*/
  char	flg_bar;		/* BAR ̂X tO	*/
  char	flg_step;		/* STEP ̂X tO	*/
  char	flg_pbend;		/* PITCH BEND ̂X tO	*/
  char	flg_vol;		/* VOLUME ̂X tO	*/
  char	flg_prg;		/* PROGRAM ̂X@tO	*/
  char	flg_panpot;		/* PANPOT ̂X tO	*/
  char	flg_midich;		/* MIDI CH ̂X tO	*/

  char	flg_song;		/* SONG f[^ tO	*/
  char	flg_system;		/* SYSTEM GÂXtO	*/ /*(v2.80)*/

  char	flg_expres;		/* EXPRESSION ̂X tO	*/
  char	flg_modula;		/* MODULATION ̂X tO	*/
  char	flg_bank;		/* BANK ̂X tO	*/
  char	flg_replay;		/* REPLAY tO		*/

  char	flg_gssys;		/* GS SYSTEM GÂXtO*/ /*(v2.93)*/
  char	flg_gsrev;		/* GSREV ̂X tO	*/ /*(v2.93)*/
  char	flg_gscho;		/* GSCHO ̂X tO	*/ /*(v2.93)*/
#endif
  char	flg_gsinfo;		/* GS info ̂X tO	*/ /*(v2.93l)*/
  char	flg_gsinst;		/* GS inst ̂X tO	*/ /*(v2.93l)*/
  char	flg_gspanel;		/* GS panel ̂X tO	*/ /*(v2.93l)*/

#if 1
  char	flg_hold1;		/* HOLD1 ̂X tO	*/
  char	flg_gsdly;		/* GSDLY ̂X tO	*/ /*(v3.00q)*/
  char	flg_bankl;		/* BANK L̂X tO	*/ /*(v3.01n)*/

  char	dummyE[9];		/* gpU[u	*/
#endif

  int	panel_tempo;		/* pl̂e|l	*/
  int	bar[ TRK_NUM ];		/* ̂	*/
  int	step[ TRK_NUM ];	/* Xebv̂	*/
#if 1
  char	vel[ TRK_NUM ];		/* xVeBl		*/
#endif

  int	stepcount;		/* tJnSTEP COUNT	*/ /*(v2.92)*/
#if 1
  int16_t	loopcount;		/* 255/256̌[s[gCOUNT	*/ /*(v3.00f)*/

  char	dummyF[12-2];		/* gpU[u	*/

  char	song[20];		/* SONGf[^(Rg)	*/

  char	dummyG[60];		/* gpU[u	*/
#endif

  char	gs_info[18];		/* gs patch name	*/ /*v2.93l*/
  char	gs_inst[34];		/* gs comment		*/ /*v2.93l*/
  uint8_t	gs_panel[64];		/* gs panel		*/ /*v2.93l*/

  char	ch_port[ CHL_NUM ];/* `li.f. 1:MIDI 2:RS-232C/POLYPHON */
#if 1
  char	ch_part[ CHL_NUM ];/* `l̂ 0:LA 1:PCM 2:(RHYTHM) */
  int	ch_pbend[ CHL_NUM ];	/* `lPITCH BENDl	*/
  char	ch_vol[ CHL_NUM ];	/* `lVOLUMEl	*/
  char	ch_panpot[ CHL_NUM ];	/* `lPANPOTl	*/
  char	flg_ch_prg[ CHL_NUM ];	/* `lPROG.CHGtO*/ /*v2.93j*/
  uint8_t ch_prg[ CHL_NUM ];/* `lPROGRAM̂	*/
  char	ch_reverb[ CHL_NUM ];	/* o[u OFF/ON	*/ /*(v2.80)*/
  char	ch_expr[ CHL_NUM ];	/* `lEXPRSSIONl	*/ /*v2.92*/
  char	ch_modu[ CHL_NUM ];	/* `lMODULATIONl	*/ /*v2.92*/
  char	ch_bank[ CHL_NUM ];	/* `lBANKl	*/ /*v2.92*/
  char	ch_gsrev[ CHL_NUM ];	/* `lGS REVERBl	*/ /*v2.93*/
  char	ch_gscho[ CHL_NUM ];	/* `lGS CHORUSl	*/ /*v2.93*/
  char	ch_hold1[ CHL_NUM ];	/* `lHOLD1l	*/ /*v2.93*/
  char	ch_gsdly[ CHL_NUM ];	/* `lGS DELAYl	*/ /*v3.00q*/
  char	ch_bankl[ CHL_NUM ];	/* `lGS BANK L.l	*/ /*v3.01n*/

  char	dummyH[34*1];		/* gpU[u	*/
#endif
};

extern struct RCD_HEAD *rcd;

#endif /* _RCDDEF_H_ */
