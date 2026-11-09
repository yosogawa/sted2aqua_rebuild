#include "abc2rcp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *msg)
{
	if (!cond) {
		fprintf(stderr, "FAIL %s\n", msg);
		fails++;
	}
}

static int find_note(const AbcSong *s, int tr, int pitch, int step, int vel)
{
	const uint8_t *p;
	int i, n;

	if (tr >= s->ntracks)
		return 0;
	p = s->trk[tr];
	n = s->len[tr];
	for (i = 0; i + 4 <= n; i += 4) {
		if (p[i] == pitch && p[i + 1] == step && p[i + 3] == vel)
			return 1;
	}
	return 0;
}

static int find_prog(const AbcSong *s, int tr, int prog)
{
	const uint8_t *p;
	int i, n;

	if (tr >= s->ntracks)
		return 0;
	p = s->trk[tr];
	n = s->len[tr];
	for (i = 0; i + 4 <= n; i += 4) {
		if (p[i] == 0xEC && p[i + 2] == prog)
			return 1;
	}
	return 0;
}

static int count_op(const AbcSong *s, int tr, int op)
{
	const uint8_t *p;
	int i, n, c = 0;

	if (tr >= s->ntracks)
		return 0;
	p = s->trk[tr];
	n = s->len[tr];
	for (i = 0; i + 4 <= n; i += 4) {
		if (p[i] == op)
			c++;
	}
	return c;
}

static void dump(const AbcSong *s, int tr)
{
	const uint8_t *p;
	int i, n;

	if (tr >= s->ntracks) {
		fprintf(stderr, "track %d missing (ntracks=%d)\n", tr, s->ntracks);
		return;
	}
	p = s->trk[tr];
	n = s->len[tr];
	fprintf(stderr, "track %d ch %d len %d\n", tr, s->mch[tr], n);
	for (i = 0; i + 4 <= n; i += 4)
		fprintf(stderr, "  %02X %3d %3d %3d\n", p[i], p[i + 1], p[i + 2], p[i + 3]);
}

int main(void)
{
	AbcSong s;
	const char *abc =
		"X:1\n"
		"T:Hello\n"
		"M:4/4\n"
		"L:1/8\n"
		"Q:1/4=90\n"
		"K:G\n"
		"c d | [CEG] | c > d | c- c | {e} f | \"C\"[CEG] | \"Am7\"c | \"Csus4\"d | !f! e |\n"
		"X:2\n"
		"K:C\n"
		"c d e f |\n";

	expect(abc_import_text(abc, (int)strlen(abc), &s) == 0, "import");
	expect(strcmp(s.title, "Hello") == 0, "title");
	expect(s.tempo == 90, "tempo");
	expect(s.beat1 == 4 && s.beat2 == 4, "meter");
	expect(s.key == 1, "G major bkey");
	expect(s.timebase == 96, "timebase");
	expect(s.mch[0] == 1, "ch A1");
	if (fails) {
		dump(&s, 0);
		dump(&s, 1);
	}
	/* c in G is C5 = 72, d = 74. Eighth = 48, gate 38. */
	expect(find_note(&s, 0, 72, 48, 100), "c");
	expect(find_note(&s, 0, 74, 48, 100), "d");
	/* f in G is F#5 = 78. */
	expect(find_note(&s, 0, 78, 48, 100), "f#");
	/* Broken c > d : 72 and 24. */
	expect(find_note(&s, 0, 72, 72, 100), "broken c");
	expect(find_note(&s, 0, 74, 24, 100), "broken d");
	/* Tie c- c is one note of 96 ticks, gate 76. */
	expect(find_note(&s, 0, 72, 96, 100), "tie");
	/* !f! e vel 115. e = 76. */
	expect(find_note(&s, 0, 76, 48, 115), "dyn e");
	/* Second tune must not add a run of 4 notes only. Bar count > 0. */
	expect(count_op(&s, 0, 0xFD) >= 4, "bars");
	/* "C"[CEG] cancels block notes. Comment remains. */
	expect(count_op(&s, 1, 0xF6) >= 1, "chord comment");
	/* Am7 sounds on the accompaniment track: A=57 or nearby. */
	expect(s.ntracks >= 2, "accomp track");
	expect(s.mch[1] == 2, "accomp ch");
	abc_song_free(&s);

	{
		const char *rep =
			"X:1\nK:C\n|: C D | E F :|\n";
		expect(abc_import_text(rep, (int)strlen(rep), &s) == 0, "rep import");
		expect(count_op(&s, 0, 0xF9) == 1, "loop start");
		expect(count_op(&s, 0, 0xF8) == 1, "loop end");
		if (s.ntracks > 0) {
			const uint8_t *p = s.trk[0];
			int i, n = s.len[0], ok = 0;
			for (i = 0; i + 4 <= n; i += 4) {
				if (p[i] == 0xF8 && p[i + 1] == 2)
					ok = 1;
			}
			expect(ok, "loop count 2");
		}
		if (fails)
			dump(&s, 0);
		abc_song_free(&s);
	}

	{
		const char *two =
			"X:1\nK:C\nV:1\nC D |\nV:2\nE F |\nV:1\nG |\n";
		expect(abc_import_text(two, (int)strlen(two), &s) == 0, "voices");
		expect(s.ntracks >= 2, "two tracks");
		expect(s.mch[0] == 1 && s.mch[1] == 2, "A1 A2");
		expect(find_note(&s, 0, 60, 48, 100), "C");
		expect(find_note(&s, 0, 67, 48, 100), "G continues V1");
		expect(find_note(&s, 1, 64, 48, 100), "E on V2");
		abc_song_free(&s);
	}

	{
		const char *ch =
			"X:1\nK:C\n\"C\"C2 | \"Am7\"E2 | \"Csus4\"G2 | \"C\"[CEG]2 |\n";
		int notes = 0, i;
		expect(abc_import_text(ch, (int)strlen(ch), &s) == 0, "chords");
		expect(s.ntracks == 2, "melody plus accomp");
		expect(count_op(&s, 1, 0xF6) >= 3, "three comments");
		if (s.ntracks > 1) {
			const uint8_t *p = s.trk[1];
			int n = s.len[1];
			for (i = 0; i + 4 <= n; i += 4) {
				if (p[i] > 0 && p[i] < 0x80 && p[i + 3] > 0)
					notes++;
			}
		}
		expect(notes == 7, "C triad + Am7, sus and [CEG] silent");
		if (notes != 7)
			dump(&s, 1);
		abc_song_free(&s);
	}

	{
		const char *rep3 = "X:1\nK:C\n|: C :|3\n";
		expect(abc_import_text(rep3, (int)strlen(rep3), &s) == 0, "rep3");
		expect(count_op(&s, 0, 0xF8) == 0, "rep3 not native");
		expect(count_op(&s, 0, 0x3C) == 3, "rep3 three attacks");
		abc_song_free(&s);
	}

	{
		const char *src =
			"X:1\nT:Hello\nM:4/4\nL:1/8\nQ:1/4=90\nK:G\n"
			"|: c d | e f :|\n"
			"\"Am7\"c2 | \"Csus4\"d2 |\n";
		char *text = NULL;
		expect(abc_import_text(src, (int)strlen(src), &s) == 0, "ex import");
		expect(abc_export_text(&s, &text) == 0 && text != NULL, "export");
		if (text) {
			expect(strstr(text, "T:Hello") != NULL, "T");
			expect(strstr(text, "M:4/4") != NULL, "M");
			expect(strstr(text, "L:1/8") != NULL, "L");
			expect(strstr(text, "Q:1/4=90") != NULL, "Q");
			expect(strstr(text, "K:G") != NULL, "K");
			expect(strstr(text, "|:") != NULL && strstr(text, ":|") != NULL,
			       "repeat marks");
			expect(strstr(text, "\"Am7\"") != NULL, "symbol out");
			expect(strstr(text, "V:2") == NULL, "no accomp voice");
			abc_song_free(&s);
			expect(abc_import_text(text, (int)strlen(text), &s) == 0,
			       "reimport");
			expect(find_note(&s, 0, 72, 48, 100), "c back");
			expect(find_note(&s, 0, 78, 48, 100), "f# back");
			expect(count_op(&s, 0, 0xF9) == 1, "loop back");
			free(text);
		}
		abc_song_free(&s);
	}

	{
		uint8_t raw[] = {
			0xEC, 0, 73, 0,
			60, 96, 76, 100,
			0xFE, 0, 0, 0
		};
		AbcSong song;
		char *text = NULL;
		const uint8_t *p;
		int i, got = 0;
		memset(&song, 0, sizeof song);
		song.tempo = 120;
		song.beat1 = 4;
		song.beat2 = 4;
		song.timebase = 96;
		song.ntracks = 1;
		song.trk[0] = raw;
		song.len[0] = (int)sizeof raw;
		expect(abc_export_text(&song, &text) == 0, "prog export");
		expect(text != NULL &&
		       strstr(text, "%%MIDI program 73 % Flute") != NULL,
		       "flute name");
		song.trk[0] = NULL;
		if (text) {
			expect(abc_import_text(text, (int)strlen(text), &s) == 0,
			       "prog reimport");
			p = s.ntracks > 0 ? s.trk[0] : NULL;
			if (p) {
				for (i = 0; i + 4 <= s.len[0]; i += 4) {
					if (p[i] == 0xEC && p[i + 2] == 73)
						got = 1;
				}
			}
			expect(got, "program 73");
			expect(find_note(&s, 0, 60, 96, 100), "note after program");
			free(text);
		}
		abc_song_free(&s);
	}

	{
		const char *ext =
			"X:1\nK:C\n"
			"V:1\n%%MIDI program 129 % Uilleann pipes\nC\n"
			"V:2\n%%MIDI program 133\nD\n"
			"V:3\n%%MIDI program 128\nE\n"
			"V:4\n%%MIDI program 200\nF\n"
			"V:5\n%%MIDI program 144\nG\n"
			"V:6\n%%MIDI program 148\nA\n"
			"V:7\n%%MIDI program 73 % Flute\nB\n";
		expect(abc_import_text(ext, (int)strlen(ext), &s) == 0, "ext import");
		expect(s.ntracks == 7, "seven voices");
		expect(find_prog(&s, 0, 109), "uilleann -> bagpipe");
		expect(find_prog(&s, 1, 22), "concertina -> harmonica");
		expect(count_op(&s, 2, 0xEC) == 0, "percussion stays unset");
		expect(find_note(&s, 2, 64, 48, 100), "percussion voice notes");
		expect(count_op(&s, 3, 0xEC) == 0, "unknown program skipped");
		expect(find_prog(&s, 4, 74), "recorder");
		expect(find_prog(&s, 5, 15), "mountain dulcimer");
		expect(find_prog(&s, 6, 73), "flute unchanged");
		abc_song_free(&s);
	}

	{
		const char *nm =
			"X:1\nK:C\n"
			"V:1 name=\"Melody\"\nC\n"
			"V:2 name=\"Skip\"\n"
			"V:4 name=\"Drums\" snm=\"Dr.\" clef=perc\nD\n"
			"V:4\nE\n";
		char abc[128];
		char expect_memo[37];
		expect(abc_import_text(nm, (int)strlen(nm), &s) == 0, "names");
		expect(s.ntracks == 2, "skip empty named voice");
		expect(s.mch[0] == 1 && s.mch[1] == 2, "channels stay in order");
		expect(strcmp(s.memo[0], "Melody") == 0, "melody memo");
		expect(strcmp(s.memo[1], "Drums") == 0, "drums memo");
		abc_song_free(&s);
		snprintf(abc, sizeof abc,
			 "X:1\nK:C\nV:1 name=\"%.*s\"\nC\n",
			 40, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcd");
		memcpy(expect_memo, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", 36);
		expect_memo[36] = 0;
		expect(abc_import_text(abc, (int)strlen(abc), &s) == 0, "long name");
		expect(strcmp(s.memo[0], expect_memo) == 0, "36 chars");
		abc_song_free(&s);
	}

	{
		const char *ch =
			"X:1\nK:C\n"
			"V:1 name=\"Melody\"\n"
			"%%MIDI channel 10\n"
			"%%MIDI program 0 % Acoustic Grand Piano\n"
			"C\n"
			"%%MIDI channel 3\n"
			"D\n"
			"\"C\"E\n";
		const uint8_t *p;
		int i, n, seen_ch = -1, seen_prog = 0, ch_before_prog = 0;
		expect(abc_import_text(ch, (int)strlen(ch), &s) == 0, "channel");
		expect(s.ntracks == 2, "melody plus chord");
		expect(s.mch[0] == 1, "header stays A1");
		expect(strcmp(s.memo[0], "Melody") == 0, "voice memo");
		expect(strcmp(s.memo[1], "Chord") == 0, "chord memo");
		p = s.trk[0];
		n = s.len[0];
		for (i = 0; i + 4 <= n; i += 4) {
			if (p[i] == 0xE6 && p[i + 1] == 0 && p[i + 2] == 10)
				seen_ch = i;
			if (p[i] == 0xEC && p[i + 2] == 0)
				seen_prog = 1;
			if (p[i] == 0xE6 && p[i + 2] == 10 &&
			    i + 4 < n && p[i + 4] == 0xEC)
				ch_before_prog = 1;
		}
		expect(seen_ch == 0, "channel 10 is first");
		expect(seen_prog && ch_before_prog, "channel before program");
		expect(find_note(&s, 0, 60, 48, 100), "C on A1 track");
		{
			int mid = 0;
			for (i = 0; i + 4 <= n; i += 4) {
				if (p[i] == 0xE6 && p[i + 2] == 3 && p[i + 1] == 0)
					mid = 1;
			}
			expect(mid, "later channel 3");
		}
		abc_song_free(&s);
		{
			const char *bad = "X:1\nK:C\n%%MIDI channel 0\nC\n";
			expect(abc_import_text(bad, (int)strlen(bad), &s) == 0,
			       "channel 0");
			expect(count_op(&s, 0, 0xE6) == 0, "channel 0 ignored");
			abc_song_free(&s);
		}
	}

	if (fails) {
		fprintf(stderr, "%d failed\n", fails);
		return 1;
	}
	printf("ok\n");
	return 0;
}
