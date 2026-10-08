#ifndef _RCDDEF_H_
#define _RCDDEF_H_

/*--------------------------------------------------------------*/
/* rcddef.h							*/
/*--------------------------------------------------------------*/
/* RCD ドライバ・アクセス用構造体定義   RCD v3.01 以降用    */
/*--------------------------------------------------------------*/
/*                                */
/* RCD を利用するプログラムは、                    */
/*                                 */
/* 本ファイルをインクルードして下さい。                */
/*                                 */
/* extern struct RCD_HEAD *rcd;   <--- ポインタの外部定義をし、    */
/*                                 */
/* rcdcheck をコールして構造体のアドレスを求めた後に、        */
/*                                 */
/* rcd->fmt などのようにアクセスする。                */
/*                                 */
/* ルーチンコールは、(*rcd->begin)() のようにする。        */
/*                                */
/*--------------------------------------------------------------*/

#define	TRK_NUM	36			/* トラック数        */
#define	CHL_NUM	34 			/* チャネル数        */
					/*  32/33 は未使用    */

#define DATA_ADR_SIZE 512 *1024          /* KB */
#define TONE_ADR_SIZE 64  *1024
#define WORD_ADR_SIZE 64  *1024
#define GSD_ADR_SIZE  64  *1024
#define SMF_ADR_SIZE  512 *1024          /* Dec.18.1998 Daisuke Nagano */


struct RCD_HEAD {
  char	title[4];		/* "RCD "		*/
  char	version[4];		/* "N.NN"		*/
  int32_t	staymark;

  uint8_t	data_valid;		/* RCP/MCP 有効時 1    */
  uint8_t	tone_valid;		/* CM6/MTD 有効時 1    */
  uint8_t	word_valid;		/* WRD 有効時 1        */
  uint8_t	fmt;			/* MCP:0 RCP:1 R36:2	*/

  uint8_t	*data_adr;		/* MCP/RCP address	*/
  uint8_t	*tone_adr;		/* MTD/CM6 address	*/
  uint8_t	*word_adr;		/* WRD address		*/
  uint8_t	*gsd_adr;		/* GSD address		*/ /*(v2.92)*/
  uint8_t	*smf_adr;		/* SMF address		*/ /* Dec.11.1998*/

  void	(*init)( void );	/* パラメータ初期化    */ /* void (v2.70)*/
  void	(*setup)( void );	/* 音色データ書き込み    */ /* void (v2.70)*/
  void	(*begin)( void );	/* 演奏開始        */ /* void (v2.70)*/
  void	(*end)( void );		/* 演奏終了        */ /* void (v2.70)*/

  void	(*md_put)( uint8_t );	  /* MIDI 1 byte 出力    */ /* void (v2.70)*/
  int32_t (*md_get)( void );	      /* MIDI 1 byte 入力　(-1)入力なし*/ /*(v2.70)*/
  void	(*md_put2)( uint8_t );	  /* MIDI 1 byte 出力(dual)*/ /* void (v2.92)*/
  void	(*mix_out)( uint8_t * );  /* MIX OUT(buff adrs)end=$ff*/ /*(v3.01)*/

  int32_t	act;			/* 1:演奏中             */
  int32_t	sts;			/* 0:RUN 1:STOP 2:SEARCH 3:CUE	*/ /*(v2.70)*/
  int32_t	tar_trk;		/* TARGET TRACK		*/
  int32_t	tar_bar;		/* TARGET BAR		*/

  uint8_t	dummy1[ 18 ];

  int32_t	tempo;			/* 現在のテンポ              */
  int32_t	basetempo;		/* 元のテンポ                */

  int32_t	totalcount;		/* 音符ファイル長    */
  char	filename[30];		/* 音符ファイル名    */
  char	tonename[30];		/* 音色ファイル名    */

  uint8_t	dummy2[ 72 ];

  int32_t	bufcap;			/* バッファ容量        */  /*(v2.70)*/

  int32_t	MIDI_avl;		/* MIDI 出力有効(trk.no)*/  /*(v2.80)*/
  uint8_t MIDI_req[16];	        /* MIDI 出力要求バッファ*/  /*(v2.80)*/
	  /* (80) (ch) ...	:チャンネルチェンジ*/
	  /* (90) (ke) (ve)	:ノートオン		*/  /*(v2.92)*/
	  /* (b0) (xx) (xx)	:コントロールチェンジ	*/
	  /* (c0) (xx) ...	:プログラムチェンジ	*/
	  /* (e0) (xx) (xx)	:ピッチベンド	*/
	  /* (f0) (hh) (mm) (ll) (xx) :メモリライト*/
	  /* (f1) (hh) (mm) (ll) (xx) (id):メモリライト*/  /*(v2.92)*/

  uint8_t	LA_VOL;			/* LA part master vol   */ /*(v2.80)*/
  uint8_t	LA_RVB_Mode;		/* LA part reverb Mode  */ /*(v2.80)*/
  uint8_t	LA_RVB_Time;		/* LA part reverb Time  */ /*(v2.80)*/
  uint8_t	LA_RVB_Level;		/* LA part reverb Level */ /*(v2.80)*/

  uint8_t	PCM_VOL;		/* PCM part master vol  */ /*(v2.80)*/
  uint8_t	PCM_RVB_Mode;		/* PCM part reverb Mode */ /*(v2.80)*/
  uint8_t	PCM_RVB_Time;		/* PCM part reverb Time */ /*(v2.80)*/
  uint8_t	PCM_RVB_Level;		/* PCM part reverb Level*/ /*(v2.80)*/

  int32_t	filter_mode;		/* 0:無効 1:PRGのみ 2:LA/PCMあり */ /*(v2.90)*/
  uint8_t	*filter_data;		/* Filter Dataアドレス	*/ /*(v2.90)*/

  int32_t	play_mode;	/* 0:normal 1:slow 2:fast 3:slow2 4:fast2 */ /*(v2.92)*/
  int32_t	mute_mode;	/* 0:off 1:cm64 2:sc55	*/ /*(v2.92)*/
  int32_t	init_mode;	/* 0:off 1:cm64 2:sc55 3:cm+sc */ /*(v2.92)*/
  uint8_t	scan_mode;	/* key scan flag 0:off 1:on */
  uint8_t	rsmd_mode;	/* midi port flag 0:midi 1:midi+rs/sb 2:rs/sb */
  int16_t	fade_time;	/* fade out speed	*/
  uint8_t	fade_count;	/* fade out start flag 128:start 0:end */
  uint8_t	moduletype;	/* panel display 0:cm64 1:sc55 2:cm+sc */
  uint8_t	fade_mode;	/* fade out mode 0:exclusive a:volume 2:expres*/
  uint8_t	timer_mode;	/* RS-MIDI timer mode 0:OPM Timer-A 1:OPM Timer-B */
  uint8_t	midi_clock;	/* MIDI clock out 0:disable 1:enable */
  uint8_t	put_mode;	/* md_put port mode 0:normal 1:midi 2:rs */
  uint8_t	rcd_type;	/* 0:midi+rs-232c 1:midi+polyphon 3:polyphon only */
  uint8_t	sc55_fix;	/* 0:off 1:sc55 capi.down emulate */ /*(v3.01)*/

  uint8_t	dummyA[19];	/* 拡張用リザーブ    */

  uint8_t	mt32_mode;	/* mt-32 mode		*/
  int32_t	exc_wait;	/* exclusive send wait	*/
  uint8_t	tim_all;	/* timbre trans mode	*/

  uint8_t	gsd_valid;	/* GSD 有効時 1		*/  /*(v2.92)*/
  char	        gsdname[30];	/* GSDファイル名	*/

  int32_t	wordcap;	/* word バッファ容量	*/  /*(v2.92)*/

  uint8_t	dummyC[10];	/* 拡張用リザーブ*/

  uint8_t	GS_VOL;		/* GS part master vol   */ /*(v2.93)*/
  uint8_t	GS_PAN;		/* GS part master pan   */ /*(v2.93)*/

  uint8_t	GS_RVB_Macro;	        /* リバーブマクロ	*/ /*(v2.93)*/
  uint8_t	GS_RVB_Char;		/* リバーブ・キャラクター・コントロール*/
  uint8_t	GS_RVB_Prelpf;		/* リバーブ・ＰＲＥ・ＬＰＦ・コントロール*/
  uint8_t	GS_RVB_Level;		/* リバーブ・レベル・コントロール*/
  uint8_t	GS_RVB_Time;		/* リバーブ・タイム・コントロール*/
  uint8_t	GS_RVB_Delay;		/* リバーブ・DELAY・FEEDBACK・コントロール**/
  uint8_t	GS_RVB_Send;		/* REVERB SEND LEVEL TO CHOURUS */
  uint8_t	GS_RVB_PreDelay;	/* REVERB PREDLY T. */

  uint8_t	GS_CHO_Macro;		/* コーラスマクロ	*/
  uint8_t	GS_CHO_Prelpf;		/* コーラス・ＰＲＥ・ＬＰＦ・コントロール*/
  uint8_t	GS_CHO_Level;		/* コーラス・レベル・コントロール*/
  uint8_t	GS_CHO_Feed;		/* コーラス・フィード・バック*/
  uint8_t	GS_CHO_Delay;		/* コーラス・Ｄｅｌａｙ・コントロール*/
  uint8_t	GS_CHO_Rate;		/* コーラス・Ｒａｔｅ・コントロール*/
  uint8_t	GS_CHO_Depth;		/* コーラス・Ｄｅｐｔｈ・コントロール*/
  uint8_t	GS_CHO_Send;		/* CHORUS SEND LEVEL TO REVERB */
  uint8_t	GS_CHO_Send_Dly;

  uint8_t	GS_DLY_Macro;		/* ディレイマクロ	*/ /*(v3.01)*/
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

  uint8_t	dummyD[32-16];		/* 拡張用リザーブ	*/

  char	active[ TRK_NUM ];	/* トラック有効		*/
  char	trk_mask[ TRK_NUM ];	/* TRACK MASK		*/
  char	midich[ TRK_NUM ];	/* MIDI CH		*/

  int32_t	noteptr;		/* ノートランニングポインタ	*/
  uint8_t	*note_adr;	/* ノートランニングバッファアドレス	*/
  char	*top[ TRK_NUM ];	/* トラックデータ先頭	*/
  uint8_t *ptr[ TRK_NUM ];	/* カレントトラックポインタ	*/

  char	flg_vel[ TRK_NUM ];	/* VELOCITY ON フラグ	*/
  char	flg_off[ TRK_NUM ];	/* VELOCITY OFF フラグ	*/
  char	flg_act;		/* ACTIVE OFF フラグ	*/
  char	flg_bar;		/* BAR 変更 フラグ	*/
  char	flg_step;		/* STEP 変更 フラグ	*/
  char	flg_pbend;		/* PITCH BEND 変更 フラグ	*/
  char	flg_vol;		/* VOLUME 変更 フラグ	*/
  char	flg_prg;		/* PROGRAM 変更フラグ	*/
  char	flg_panpot;		/* PANPOT 変更 フラグ	*/
  char	flg_midich;		/* MIDI CH 変更 フラグ	*/

  char	flg_song;		/* SONG データ フラグ	*/
  char	flg_system;		/* SYSTEM エリア変更フラグ	*/ /*(v2.80)*/

  char	flg_expres;		/* EXPRESSION 変更 フラグ	*/
  char	flg_modula;		/* MODULATION 変更 フラグ	*/
  char	flg_bank;		/* BANK 変更 フラグ	*/
  char	flg_replay;		/* REPLAY フラグ		*/

  char	flg_gssys;		/* GS SYSTEM エリア変更フラグ*/ /*(v2.93)*/
  char	flg_gsrev;		/* GSREV 変更 フラグ	*/ /*(v2.93)*/
  char	flg_gscho;		/* GSCHO 変更 フラグ	*/ /*(v2.93)*/
  char	flg_gsinfo;		/* GS info 変更 フラグ	*/ /*(v2.93l)*/
  char	flg_gsinst;		/* GS inst 変更 フラグ	*/ /*(v2.93l)*/
  char	flg_gspanel;		/* GS panel 変更 フラグ	*/ /*(v2.93l)*/

  char	flg_hold1;		/* HOLD1 変更 フラグ	*/
  char	flg_gsdly;		/* GSDLY 変更 フラグ	*/ /*(v3.00q)*/
  char	flg_bankl;		/* BANK L変更 フラグ	*/ /*(v3.01n)*/

  uint8_t	dummyE[9];		/* 拡張用リザーブ	*/

  int32_t	panel_tempo;		/* パネル上のテンポ値	*/
  int32_t	bar[ TRK_NUM ];		/* 小節番号	*/
  int32_t	step[ TRK_NUM ];	/* ステップ番号	*/
  char  	vel[ TRK_NUM ];		/* ベロシティ値		*/

  int32_t	stepcount;		/* 演奏開始時からのSTEP COUNT	*/ /*(v2.92)*/
  int16_t	loopcount;		/* 255/256回リピートのCOUNT	*/ /*(v3.00f)*/

  uint8_t	dummyF[12-2];		/* 拡張用リザーブ	*/

  char  	song[20];		/* SONGデータ(コメント)	*/

  uint8_t	dummyG[60];		/* 拡張用リザーブ	*/

  char  	gs_info[18];		/* gs patch name	*/ /*v2.93l*/
  char  	gs_inst[34];		/* gs comment		*/ /*v2.93l*/
  uint8_t	gs_panel[64];		/* gs panel		*/ /*v2.93l*/

  char  	ch_port[ CHL_NUM ];     /* チャネル毎の1.f.種類 1:MIDI 2:RS-232C/POLYPHON */
  char  	ch_part[ CHL_NUM ];     /* チャネル毎の音源種類 0:LA 1:PCM 2:(RHYTHM) */
  int32_t	ch_pbend[ CHL_NUM ];	/* チャネル毎のPITCH BENDl	*/
  char  	ch_vol[ CHL_NUM ];	/* チャネル毎のVOLUMEl	*/
  char  	ch_panpot[ CHL_NUM ];	/* チャネル毎のPANPOTl	*/
  char  	flg_ch_prg[ CHL_NUM ];	/* チャネル毎のPROG.CHGtO*/ /*v2.93j*/
  uint8_t       ch_prg[ CHL_NUM ];      /* チャネル毎のPROGRAM̂	*/
  char  	ch_reverb[ CHL_NUM ];	/* リバーブ OFF/ON	*/ /*(v2.80)*/
  char  	ch_expr[ CHL_NUM ];	/* チャネル毎のEXPRSSION値	*/ /*v2.92*/
  char  	ch_modu[ CHL_NUM ];	/* チャネル毎のMODULATION値	*/ /*v2.92*/
  char  	ch_bank[ CHL_NUM ];	/* チャネル毎のBANK値	*/ /*v2.92*/
  char  	ch_gsrev[ CHL_NUM ];	/* チャネル毎のGS REVERB値	*/ /*v2.93*/
  char  	ch_gscho[ CHL_NUM ];	/* チャネル毎のGS CHORUS値	*/ /*v2.93*/
  char  	ch_hold1[ CHL_NUM ];	/* チャネル毎のHOLD1値	*/ /*v2.93*/
  char  	ch_gsdly[ CHL_NUM ];	/* チャネル毎のGS DELAY値	*/ /*v3.00q*/
  char  	ch_bankl[ CHL_NUM ];	/* チャネル毎のGS BANK L.値	*/ /*v3.01n*/

  uint8_t	dummyH[34*1];		/* 拡張用リザーブ	*/
};

extern struct RCD_HEAD *rcd;

#endif /* _RCDDEF_H_ */
