/* struct definition for G36 file */

#ifndef _G36STRUCT_H_
#define _G36STRUCT_H_

#include <stdint.h>

typedef struct G36HEAD {
  uint8_t mark[32];
  uint8_t title[128];
  uint8_t memo[360];
  uint8_t trkmax;
  uint8_t x00[1];
  uint8_t Timebase;
  uint8_t Timebaseh;
  uint8_t Tempo;
  uint8_t Tempoh;
  uint8_t Beathigh;
  uint8_t Beatlow;
  uint8_t Key;
  uint8_t Playbias;
  uint8_t x00b[6];
  uint8_t x40[16];
  uint8_t x00c[112];
  uint8_t CM6name[16];
  uint8_t GSDname[16];
  uint8_t GSDname2[16];
  uint8_t dumy[80];
  uint8_t Rhythm[128][16];
  uint8_t Exclusive[8][48];
} G36HEAD;

typedef struct G36TRACK {
  uint8_t len_low;
  uint8_t len_high;
  uint8_t len_high2;
  uint8_t len_high3;
  uint8_t track_no;
  uint8_t rhythm_flag;
  uint8_t MIDI_CH;
  uint8_t Play_bias;
  uint8_t ST_offset;
  uint8_t Mode;
  uint8_t Memo[36];
} G36TRACK;

#endif /* _G36STRUCT_H_ */
