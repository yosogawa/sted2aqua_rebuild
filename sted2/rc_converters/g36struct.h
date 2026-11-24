/* struct definition for G36 file */

#ifndef _G36STRUCT_H_
#define _G36STRUCT_H_

#define uchar unsigned char

struct G36HEAD {
  uchar mark[32];
  uchar title[128];
  uchar memo[360];
  uchar trkmax;
  uchar x00[1];
  uchar Timebase;
  uchar Timebaseh;
  uchar Tempo;
  uchar Tempoh;
  uchar Beathigh;
  uchar Beatlow;
  uchar Key;
  uchar Playbias;
  uchar x00b[6];
  uchar x40[16];
  uchar x00c[112];
  uchar CM6name[16];
  uchar GSDname[16];
  uchar GSDname2[16];
  uchar dumy[80];
  uchar Rhythm[128][16];
  uchar Exclusive[8][48];
} G36_HEAD;

struct G36TRACK {
  uchar len_low;
  uchar len_high;
  uchar len_high2;
  uchar len_high3;
  uchar track_no;
  uchar rhythm_flag;
  uchar MIDI_CH;
  uchar Play_bias;
  uchar ST_offset;
  uchar Mode;
  uchar Memo[36];
} G36_TRACK;

#endif /* _G36STRUCT_H_ */
