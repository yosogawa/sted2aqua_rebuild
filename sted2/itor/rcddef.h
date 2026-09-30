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
/*								*/
/*--------------------------------------------------------------*/

#define	TRK_NUM	36			/* トラック数		*/
#define	CHL_NUM	34 			/* チャネル数		*/
					/*  32/33 は未使用	*/

struct RCD_HEAD {
	char	title[4];		/* "RCD "		*/
	char	version[4];		/* "N.NN"		*/
	int	staymark;

	uint8_t	data_valid;		/* RCP/MCP 有効時 1	*/
	uint8_t	tone_valid;		/* CM6/MTD 有効時 1	*/
	uint8_t	word_valid;		/* WRD 有効時 1		*/
	uint8_t	fmt;			/* MCP:0 RCP:1 R36:2	*/

	uint8_t	*data_adr;		/* MCP/RCP address	*/
	uint8_t	*tone_adr;		/* MTD/CM6 address	*/
	uint8_t	*word_adr;		/* WRD address		*/
	uint8_t	*gsd_adr;		/* GSD address		*/ /*(v2.92)*/

	void	(*init)( void );	/* パラメータ初期化	*/ /* void (v2.70)*/
	void	(*setup)( void );	/* 音色データ書き込み	*/ /* void (v2.70)*/
	void	(*begin)( void );	/* 演奏開始		*/ /* void (v2.70)*/
	void	(*end)( void );		/* 演奏終了		*/ /* void (v2.70)*/

	void	(*md_put)( uint8_t );	/* MIDI 1 byte 出力	*/ /* void (v2.70)*/
	int	(*md_get)( void );	/* MIDI 1 byte 入力 (-1)入力なし*/ /*(v2.70)*/
	void	(*md_put2)( uint8_t );	/* MIDI 1 byte 出力(dual)*/ /* void (v2.92)*/
	void	(*mix_out)( uint8_t * );	/* MIX OUT(buff adrs)end=$ff*/ /*(v3.01)*/

	int	act;			/* 1:演奏中		*/
	int	sts;			/* 0:RUN 1:STOP 2:SEARCH 3:CUE	*/ /*(v2.70)*/
	int	tar_trk;		/* TARGET TRACK		*/
	int	tar_bar;		/* TARGET BAR		*/

	char	dummy1[ 18 ];

	int	tempo;			/* 現在のテンポ		*/
	int	basetempo;		/* 元のテンポ		*/

	int	totalcount;		/* 音符ファイル長	*/
	char	filename[30];		/* 音符ファイル名	*/
	char	tonename[30];		/* 音色ファイル名	*/

	char	dummy2[ 72 ];

	int	bufcap;			/* �Хåե�����		*/  /*(v2.70)*/

	int	MIDI_avl;		/* MIDI Ω–ŒœÕ≠∏˙(trk.no)*/  /*(v2.80)*/
	uint8_t MIDI_req[16];	/* MIDI Ω–ŒœÕ◊µ·•–•√•’•°*/  /*(v2.80)*/
		/* (80) (ch) ...	:¡¨»Ÿ¡™›º*/
		/* (90) (ke) (ve)	:…∞ƒµ	*/  /*(v2.92)*/
		/* (b0) (xx) (xx)	:∫›ƒ€∞Ÿ¡™›º*/
		/* (c0) (xx) ...	:Ãﬂ€∏ﬁ◊—¡™›ºﬁ	*/
		/* (e0) (xx) (xx)	:ÀﬂØ¡Õﬁ›¿ﬁ∞	*/
		/* (f0) (hh) (mm) (ll) (xx) :“”ÿ◊≤ƒ	*/
		/* (f1) (hh) (mm) (ll) (xx) (id):“”ÿ◊≤ƒ	*/  /*(v2.92)*/

	char	LA_VOL;			/* LA part master vol   */ /*(v2.80)*/
	char	LA_RVB_Mode;		/* LA part reverb Mode  */ /*(v2.80)*/
	char	LA_RVB_Time;		/* LA part reverb Time  */ /*(v2.80)*/
	char	LA_RVB_Level;		/* LA part reverb Level */ /*(v2.80)*/

	char	PCM_VOL;		/* PCM part master vol  */ /*(v2.80)*/
	char	PCM_RVB_Mode;		/* PCM part reverb Mode */ /*(v2.80)*/
	char	PCM_RVB_Time;		/* PCM part reverb Time */ /*(v2.80)*/
	char	PCM_RVB_Level;		/* PCM part reverb Level*/ /*(v2.80)*/

	int	filter_mode;		/* 0:Ãµ∏˙ 1:PRG§Œ§ﬂ 2:LA/PCM§¢§Í */ /*(v2.90)*/
	uint8_t	*filter_data;		/* Filter Data•¢•…•Ï•π	*/ /*(v2.90)*/

	int	play_mode;		/* 0:normal 1:slow 2:fast 3:slow2 4:fast2 */ /*(v2.92)*/
	int	mute_mode;		/* 0:off 1:cm64 2:sc55	*/ /*(v2.92)*/
	int	init_mode;		/* 0:off 1:cm64 2:sc55 3:cm+sc */ /*(v2.92)*/
	uint8_t	scan_mode;		/* key scan flag 0:off 1:on */
	uint8_t	rsmd_mode;		/* midi port flag 0:midi 1:midi+rs/sb 2:rs/sb */
	int16_t	fade_time;		/* fade out speed	*/
	uint8_t	fade_count;		/* fade out start flag 128:start 0:end */
	uint8_t	moduletype;		/* panel display 0:cm64 1:sc55 2:cm+sc */
	uint8_t	fade_mode;		/* fade out mode 0:exclusive a:volume 2:expres*/
	uint8_t	timer_mode;		/* RS-MIDI timer mode 0:OPM Timer-A 1:OPM Timer-B */
	uint8_t	midi_clock;		/* MIDI clock out 0:disable 1:enable */
	uint8_t	put_mode;		/* md_put port mode 0:normal 1:midi 2:rs */
	uint8_t	rcd_type;		/* 0:midi+rs-232c 1:midi+polyphon 3:polyphon only */
	uint8_t	sc55_fix;		/* 0:off 1:sc55 capi.down emulate */ /*(v3.01)*/

	char	dummyA[19];		/* ≥»ƒ•Õ—•Í•∂°º•÷	*/

	uint8_t	mt32_mode;		/* mt-32 mode		*/
	int	exc_wait;		/* exclusive send wait	*/
	uint8_t	tim_all;		/* timbre trans mode	*/

	uint8_t	gsd_valid;		/* GSD Õ≠∏˙ª˛ 1		*/  /*(v2.92)*/
	char	gsdname[30];		/* GSD•’•°•§•ÎÃæ	*/

	int	wordcap;		/* word •–•√•’•°Õ∆ŒÃ	*/  /*(v2.92)*/

	char	dummyC[10];		/* ≥»ƒ•Õ—•Í•∂°º•÷	*/

	uint8_t	GS_VOL;			/* GS part master vol   */ /*(v2.93)*/
	uint8_t	GS_PAN;			/* GS part master pan   */ /*(v2.93)*/

	uint8_t	GS_RVB_Macro;		/* •Í•–°º•÷•ﬁ•Ø•Ì	*/ /*(v2.93)*/
	uint8_t	GS_RVB_Char;		/* •Í•–°º•÷°¶•≠•„•È•Ø•ø°º°¶•≥•Û•»•Ì°º•Î*/
	uint8_t	GS_RVB_Prelpf;		/* •Í•–°º•÷°¶£–£“£≈°¶£Ã£–£∆°¶•≥•Û•»•Ì°º•Î*/
	uint8_t	GS_RVB_Level;		/* •Í•–°º•÷°¶•Ï•Ÿ•Î°¶•≥•Û•»•Ì°º•Î*/
	uint8_t	GS_RVB_Time;		/* •Í•–°º•÷°¶•ø•§•‡°¶•≥•Û•»•Ì°º•Î*/
	uint8_t	GS_RVB_Delay;		/* •Í•–°º•÷°¶DELAY°¶FEEDBACK°¶•≥•Û•»•Ì°º•Î*/
	uint8_t	GS_RVB_Send;		/* REVERB SEND LEVEL TO CHOURUS */
	uint8_t	GS_RVB_PreDelay;	/* REVERB PREDLY T. */

	uint8_t	GS_CHO_Macro;		/* •≥°º•È•π•ﬁ•Ø•Ì	*/
	uint8_t	GS_CHO_Prelpf;		/* •≥°º•È•π°¶£–£“£≈°¶£Ã£–£∆°¶•≥•Û•»•Ì°º•Î*/
	uint8_t	GS_CHO_Level;		/* •≥°º•È•π°¶•Ï•Ÿ•Î°¶•≥•Û•»•Ì°º•Î*/
	uint8_t	GS_CHO_Feed;		/* •≥°º•È•π°¶•’•£°º•…°¶•–•√•Ø*/
	uint8_t	GS_CHO_Delay;		/* •≥°º•È•π°¶£ƒ£Â£Ï£·£˘°¶•≥•Û•»•Ì°º•Î*/
	uint8_t	GS_CHO_Rate;		/* •≥°º•È•π°¶£“£·£Ù£Â°¶•≥•Û•»•Ì°º•Î*/
	char	GS_CHO_Depth;		/* •≥°º•È•π°¶£ƒ£Â££Ù£Ë°¶•≥•Û•»•Ì°º•Î*/
	uint8_t	GS_CHO_Send;		/* CHORUS SEND LEVEL TO REVERB */
	uint8_t	GS_CHO_Send_Dly;

	uint8_t	GS_DLY_Macro;		/* •«•£•Ï•§•ﬁ•Ø•Ì	*/ /*(v3.01)*/
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

	char	dummyD[32-16];		/* ≥»ƒ•Õ—•Í•∂°º•÷	*/

	char	active[ TRK_NUM ];	/* •»•È•√•ØÕ≠∏˙		*/
	char	trk_mask[ TRK_NUM ];	/* TRACK MASK		*/
	char	midich[ TRK_NUM ];	/* MIDI CH		*/

	int	noteptr;		/* …∞ƒ◊›∆›∏ﬁŒﬂ≤›¿	*/
	uint8_t	*note_adr;	/* …∞ƒ◊›∆›∏ﬁ ﬁØÃß±ƒﬁ⁄*/
	char	*top[ TRK_NUM ];	/* •»•È•√•Ø•«°º•ø¿Ë∆¨	*/
	uint8_t *ptr[ TRK_NUM ];	/* ∂⁄›ƒƒ◊Ø∏Œﬂ≤›*/

	char	flg_vel[ TRK_NUM ];	/* VELOCITY ON Ã◊∏ﬁ	*/
	char	flg_off[ TRK_NUM ];	/* VELOCITY OFF Ã◊∏ﬁ	*/
	char	flg_act;		/* ACTIVE OFF Ã◊∏ﬁ	*/
	char	flg_bar;		/* BAR  —ππ Ã◊∏ﬁ	*/
	char	flg_step;		/* STEP  —ππ Ã◊∏ﬁ	*/
	char	flg_pbend;		/* PITCH BEND  —ππ Ã◊∏ﬁ	*/
	char	flg_vol;		/* VOLUME  —ππ Ã◊∏ﬁ	*/
	char	flg_prg;		/* PROGRAM  —ππ°°Ã◊∏ﬁ	*/
	char	flg_panpot;		/* PANPOT  —ππ Ã◊∏ﬁ	*/
	char	flg_midich;		/* MIDI CH  —ππ Ã◊∏ﬁ	*/

	char	flg_song;		/* SONG √ﬁ∞¿ Ã◊∏ﬁ	*/
	char	flg_system;		/* SYSTEM ¥ÿXÃ◊∏ﬁ	*/ /*(v2.80)*/

	char	flg_expres;		/* EXPRESSION  —ππ Ã◊∏ﬁ	*/
	char	flg_modula;		/* MODULATION  —ππ Ã◊∏ﬁ	*/
	char	flg_bank;		/* BANK  —ππ Ã◊∏ﬁ	*/
	char	flg_replay;		/* REPLAY Ã◊∏ﬁ		*/

	char	flg_gssys;		/* GS SYSTEM ¥ÿXÃ◊∏ﬁ*/ /*(v2.93)*/
	char	flg_gsrev;		/* GSREV  —ππ Ã◊∏ﬁ	*/ /*(v2.93)*/
	char	flg_gscho;		/* GSCHO  —ππ Ã◊∏ﬁ	*/ /*(v2.93)*/
	char	flg_gsinfo;		/* GS info  —ππ Ã◊∏ﬁ	*/ /*(v2.93l)*/
	char	flg_gsinst;		/* GS inst  —ππ Ã◊∏ﬁ	*/ /*(v2.93l)*/
	char	flg_gspanel;		/* GS panel  —ππ Ã◊∏ﬁ	*/ /*(v2.93l)*/

	char	flg_hold1;		/* HOLD1  —ππ Ã◊∏ﬁ	*/
	char	flg_gsdly;		/* GSDLY  —ππ Ã◊∏ﬁ	*/ /*(v3.00q)*/
	char	flg_bankl;		/* BANK L —ππ Ã◊∏ﬁ	*/ /*(v3.01n)*/

	char	dummyE[9];		/* ≥»ƒ•Õ—•Í•∂°º•÷	*/

	int	panel_tempo;		/* •—•Õ•ÎæÂ§Œ•∆•Û•›√Õ	*/
	int	bar[ TRK_NUM ];		/* æÆ¿·»÷πÊ		*/
	int	step[ TRK_NUM ];	/* •π•∆•√•◊»÷πÊ		*/
	char	vel[ TRK_NUM ];		/* •Ÿ•Ì•∑•∆•£√Õ		*/

	int	stepcount;		/* ±È¡’≥´ªœª˛§´§È§ŒSTEP COUNT	*/ /*(v2.92)*/
	int16_t	loopcount;		/* 255/256≤Û•Í•‘°º•»§ŒCOUNT	*/ /*(v3.00f)*/

	char	dummyF[12-2];		/* ≥»ƒ•Õ—•Í•∂°º•÷	*/

	char	song[20];		/* SONG•«°º•ø(•≥•·•Û•»)	*/
	char	dummyG[60];		/* ≥»ƒ•Õ—•Í•∂°º•÷	*/

	char	gs_info[18];		/* gs patch name	*/ /*v2.93l*/
	char	gs_inst[34];		/* gs comment		*/ /*v2.93l*/
	uint8_t	gs_panel[64];		/* gs panel		*/ /*v2.93l*/

	char	ch_port[ CHL_NUM ];	/* ¡¨»ŸÀË§Œi.f.ºÔŒ‡ 1:MIDI 2:RS-232C/POLYPHON */
	char	ch_part[ CHL_NUM ];	/* ¡¨»ŸÀË§Œ≤ª∏ªºÔŒ‡ 0:LA 1:PCM 2:¬æ(RHYTHM) */
	int	ch_pbend[ CHL_NUM ];	/* ¡¨»ŸÀË§ŒPITCH BEND√Õ	*/
	char	ch_vol[ CHL_NUM ];	/* ¡¨»ŸÀË§ŒVOLUME√Õ	*/
	char	ch_panpot[ CHL_NUM ];	/* ¡¨»ŸÀË§ŒPANPOT√Õ	*/
	char	flg_ch_prg[ CHL_NUM ];	/* ¡¨»ŸÀË§ŒPROG.CHG•’•È•∞*/ /*v2.93j*/
	uint8_t ch_prg[ CHL_NUM ];/* ¡¨»ŸÀË§ŒPROGRAM»÷πÊ	*/
	char	ch_reverb[ CHL_NUM ];	/* •Í•–°º•÷ OFF/ON	*/ /*(v2.80)*/
	char	ch_expr[ CHL_NUM ];	/* ¡¨»ŸÀË§ŒEXPRSSION√Õ	*/ /*v2.92*/
	char	ch_modu[ CHL_NUM ];	/* ¡¨»ŸÀË§ŒMODULATION√Õ	*/ /*v2.92*/
	char	ch_bank[ CHL_NUM ];	/* ¡¨»ŸÀË§ŒBANK√Õ	*/ /*v2.92*/
	char	ch_gsrev[ CHL_NUM ];	/* ¡¨»ŸÀË§ŒGS REVERB√Õ	*/ /*v2.93*/
	char	ch_gscho[ CHL_NUM ];	/* ¡¨»ŸÀË§ŒGS CHORUS√Õ	*/ /*v2.93*/
	char	ch_hold1[ CHL_NUM ];	/* ¡¨»ŸÀË§ŒHOLD1√Õ	*/ /*v2.93*/
	char	ch_gsdly[ CHL_NUM ];	/* ¡¨»ŸÀË§ŒGS DELAY√Õ	*/ /*v3.00q*/
	char	ch_bankl[ CHL_NUM ];	/* ¡¨»ŸÀË§ŒGS BANK L.√Õ	*/ /*v3.01n*/

	char	dummyH[34*1];		/* ≥»ƒ•Õ—•Í•∂°º•÷	*/
};
