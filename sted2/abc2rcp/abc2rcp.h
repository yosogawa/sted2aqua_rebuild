/*
 * ABC <-> RCP for the file menu. One tune, whole song, timebase 96.
 * The core does not touch editor globals. file.c installs the result.
 */
#ifndef ABC2RCP_H
#define ABC2RCP_H

#include <stdint.h>

#define ABC_MAX_TRACKS 36
#define ABC_TIMEBASE   96

typedef struct AbcSong {
	char title[65];
	int tempo;
	int beat1;
	int beat2;
	int key; /* STed bkey, 0..31 */
	int timebase;
	int ntracks;
	uint8_t *trk[ABC_MAX_TRACKS];
	int len[ABC_MAX_TRACKS];
	int mch[ABC_MAX_TRACKS]; /* 1=A1 .. 16=A16, 17=B1 .. 32=B16, 0=none */
	char memo[ABC_MAX_TRACKS][37]; /* V: name=, 36 chars + NUL */
} AbcSong;

/* 0 on success. On failure song is left empty. */
int abc_import_text(const char *text, int text_len, AbcSong *song);

/* Writes a NUL-terminated ABC text. Caller frees *out. */
int abc_export_text(const AbcSong *song, char **out);

void abc_song_free(AbcSong *song);

#endif
