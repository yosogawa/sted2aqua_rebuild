/*
 * RCP tracks -> one ABC tune.
 * L:1/8, durations are exact fractions of an eighth.
 * Velocity is not written. A program change is a %%MIDI program
 * line, with the GM name after % so other readers treat it as a comment.
 */
#include "abc2rcp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A B C D E F G */
static const int k_pc[7] = {9, 11, 0, 2, 4, 5, 7};
static const int k_up[7] = {69, 71, 60, 62, 64, 65, 67};
static const int k_sharp_let[7] = {5, 2, 6, 3, 0, 4, 1};
static const int k_flat_let[7] = {1, 4, 0, 3, 6, 2, 5};
static const char k_name[7] = {'A', 'B', 'C', 'D', 'E', 'F', 'G'};

/* General MIDI Level 1, program 0 .. 127. */
static const char *gm_program[128] = {
	"Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano",
	"Honky-tonk Piano", "Electric Piano 1", "Electric Piano 2",
	"Harpsichord", "Clavinet", "Celesta", "Glockenspiel", "Music Box",
	"Vibraphone", "Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
	"Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ",
	"Reed Organ", "Accordion", "Harmonica", "Tango Accordion",
	"Acoustic Guitar (nylon)", "Acoustic Guitar (steel)",
	"Electric Guitar (jazz)", "Electric Guitar (clean)",
	"Electric Guitar (muted)", "Overdriven Guitar", "Distortion Guitar",
	"Guitar Harmonics", "Acoustic Bass", "Electric Bass (finger)",
	"Electric Bass (pick)", "Fretless Bass", "Slap Bass 1", "Slap Bass 2",
	"Synth Bass 1", "Synth Bass 2", "Violin", "Viola", "Cello",
	"Contrabass", "Tremolo Strings", "Pizzicato Strings", "Orchestral Harp",
	"Timpani", "String Ensemble 1", "String Ensemble 2", "Synth Strings 1",
	"Synth Strings 2", "Choir Aahs", "Voice Oohs", "Synth Voice",
	"Orchestra Hit", "Trumpet", "Trombone", "Tuba", "Muted Trumpet",
	"French Horn", "Brass Section", "Synth Brass 1", "Synth Brass 2",
	"Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe",
	"English Horn", "Bassoon", "Clarinet", "Piccolo", "Flute", "Recorder",
	"Pan Flute", "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
	"Lead 1 (square)", "Lead 2 (sawtooth)", "Lead 3 (calliope)",
	"Lead 4 (chiff)", "Lead 5 (charang)", "Lead 6 (voice)",
	"Lead 7 (fifths)", "Lead 8 (bass + lead)", "Pad 1 (new age)",
	"Pad 2 (warm)", "Pad 3 (polysynth)", "Pad 4 (choir)", "Pad 5 (bowed)",
	"Pad 6 (metallic)", "Pad 7 (halo)", "Pad 8 (sweep)", "FX 1 (rain)",
	"FX 2 (soundtrack)", "FX 3 (crystal)", "FX 4 (atmosphere)",
	"FX 5 (brightness)", "FX 6 (goblins)", "FX 7 (echoes)", "FX 8 (sci-fi)",
	"Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Bagpipe", "Fiddle",
	"Shanai", "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock",
	"Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
	"Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet",
	"Telephone Ring", "Helicopter", "Applause", "Gunshot"
};
_Static_assert(sizeof gm_program / sizeof gm_program[0] == 128,
	       "GM Level 1 has 128 programs");

static const char *key_abc[32] = {
	"C", "G", "D", "A", "E", "B", "F#", "C#",
	"C", "F", "Bb", "Eb", "Ab", "Db", "Gb", "Cb",
	"Am", "Em", "Bm", "F#m", "C#m", "G#m", "D#m", "A#m",
	"Am", "Dm", "Gm", "Cm", "Fm", "Bbm", "Ebm", "Abm"
};

typedef struct Out {
	char *p;
	int n;
	int cap;
	int col;
} Out;

typedef struct Sym {
	int tick;
	char text[48];
} Sym;

typedef struct Grp {
	int pit[8];
	int np;
	int step;
	int gate;
} Grp;

static int out_grow(Out *o, int add)
{
	while (o->n + add + 1 > o->cap) {
		int cap = o->cap ? o->cap * 2 : 256;
		char *np = (char *)realloc(o->p, (size_t)cap);
		if (np == NULL)
			return -1;
		o->p = np;
		o->cap = cap;
	}
	return 0;
}

static int out_raw(Out *o, const char *s)
{
	int i, len = (int)strlen(s);

	if (out_grow(o, len) != 0)
		return -1;
	memcpy(o->p + o->n, s, (size_t)len);
	o->n += len;
	o->p[o->n] = 0;
	for (i = 0; i < len; i++) {
		if (s[i] == '\n')
			o->col = 0;
		else
			o->col++;
	}
	return 0;
}

static int out_tok(Out *o, const char *s)
{
	int len = (int)strlen(s);

	if (len == 0)
		return 0;
	if (o->col > 0 && o->col + 1 + len > 72) {
		if (out_raw(o, "\n") != 0)
			return -1;
	} else if (o->col > 0) {
		if (out_raw(o, " ") != 0)
			return -1;
	}
	return out_raw(o, s);
}

static int gcd_i(int a, int b)
{
	if (a < 0)
		a = -a;
	if (b < 0)
		b = -b;
	while (b) {
		int t = a % b;
		a = b;
		b = t;
	}
	return a ? a : 1;
}

static void sig_of(int bkey, int sig[7])
{
	int n, i;

	bkey &= 31;
	if (bkey < 8)
		n = bkey;
	else if (bkey < 16)
		n = -(bkey - 8);
	else if (bkey < 24)
		n = bkey - 16;
	else
		n = -(bkey - 24);
	if (n > 7)
		n = 7;
	if (n < -7)
		n = -7;
	for (i = 0; i < 7; i++)
		sig[i] = 0;
	if (n > 0) {
		for (i = 0; i < n; i++)
			sig[k_sharp_let[i]] = 1;
	} else if (n < 0) {
		for (i = 0; i < -n; i++)
			sig[k_flat_let[i]] = -1;
	}
}

static void spell(char *dst, int midi, const int sig[7])
{
	int pc, let, best, bacc, bscore, natural, delta, marks, i;
	char *d = dst;

	pc = midi % 12;
	if (pc < 0)
		pc += 12;
	best = 2;
	bacc = 0;
	bscore = 1000;
	for (let = 0; let < 7; let++) {
		int acc = pc - k_pc[let];
		int score;
		while (acc > 6)
			acc -= 12;
		while (acc < -6)
			acc += 12;
		if (acc > 2 || acc < -2)
			score = 50 + (acc < 0 ? -acc : acc);
		else if (acc == sig[let])
			score = 0;
		else
			score = 10 + (acc > sig[let] ? acc - sig[let] : sig[let] - acc);
		if (score < bscore) {
			bscore = score;
			best = let;
			bacc = acc;
		}
	}
	if (bacc != sig[best]) {
		if (bacc == 0)
			*d++ = '=';
		else if (bacc == 1)
			*d++ = '^';
		else if (bacc == 2) {
			*d++ = '^';
			*d++ = '^';
		} else if (bacc == -1)
			*d++ = '_';
		else if (bacc == -2) {
			*d++ = '_';
			*d++ = '_';
		}
	}
	natural = midi - bacc;
	delta = (natural - k_up[best]) / 12;
	if (delta >= 1) {
		*d++ = (char)(k_name[best] - 'A' + 'a');
		marks = delta - 1;
		if (marks > 4)
			marks = 4;
		for (i = 0; i < marks; i++)
			*d++ = '\'';
	} else {
		*d++ = k_name[best];
		marks = -delta;
		if (marks > 4)
			marks = 4;
		for (i = 0; i < marks; i++)
			*d++ = ',';
	}
	*d = 0;
}

static void add_len(char *dst, int ticks, int eighth)
{
	int n, d, g;
	char *e;

	if (ticks <= 0 || eighth <= 0)
		return;
	n = ticks;
	d = eighth;
	g = gcd_i(n, d);
	n /= g;
	d /= g;
	e = dst + strlen(dst);
	if (n == 1 && d == 1)
		return;
	if (d == 1)
		sprintf(e, "%d", n);
	else if (n == 1)
		sprintf(e, "/%d", d);
	else
		sprintf(e, "%d/%d", n, d);
}

static int is_pad(const uint8_t *p)
{
	return p[0] == 0 && p[1] == 0;
}

static int is_rest_ev(const uint8_t *p)
{
	return p[0] < 0x80 && (p[0] == 0 || p[2] == 0 || p[3] == 0) && p[1] > 0;
}

static int is_tone(const uint8_t *p)
{
	return p[0] > 0 && p[0] < 0x80 && p[2] > 0 && p[3] > 0;
}

static int read_grp(const uint8_t *p, int n, int i, Grp *g)
{
	memset(g, 0, sizeof *g);
	if (i + 4 > n || p[i] >= 0x80)
		return i;
	if (!is_tone(p + i)) {
		g->step = p[i + 1];
		return i + 4;
	}
	while (i + 4 <= n && is_tone(p + i)) {
		if (g->np < 8)
			g->pit[g->np++] = p[i];
		g->gate = p[i + 2];
		if (p[i + 1] > 0) {
			g->step = p[i + 1];
			return i + 4;
		}
		i += 4;
	}
	return i;
}

static int same_pits(const Grp *a, const Grp *b)
{
	int i;

	if (a->np == 0 || a->np != b->np)
		return 0;
	for (i = 0; i < a->np; i++) {
		if (a->pit[i] != b->pit[i])
			return 0;
	}
	return 1;
}

static int skip_pads(const uint8_t *p, int n, int i)
{
	while (i + 4 <= n && p[i] < 0x80 && is_pad(p + i))
		i += 4;
	return i;
}

/* Join a held note with the slices that keep it sounding. */
static int merge_hold(const uint8_t *p, int n, int i, Grp *g)
{
	int gate = g->gate;
	int slice = g->step;
	int total = g->step;

	if (g->np == 0)
		return i;
	while (gate >= slice) {
		Grp nxt;
		int j = skip_pads(p, n, i);
		if (j + 4 > n || p[j] >= 0x80 || !is_tone(p + j))
			break;
		j = read_grp(p, n, j, &nxt);
		if (!same_pits(g, &nxt) || nxt.step <= 0)
			break;
		total += nxt.step;
		gate = nxt.gate;
		slice = nxt.step;
		i = j;
	}
	g->step = total;
	return i;
}

static int merge_rests(const uint8_t *p, int n, int i, int *step)
{
	int total = *step;

	for (;;) {
		int j = skip_pads(p, n, i);
		if (j + 4 > n || !is_rest_ev(p + j))
			break;
		total += p[j + 1];
		i = j + 4;
	}
	*step = total;
	return i;
}

static int take_text(const uint8_t *p, int n, int i, char *dst)
{
	int k = 0;

	if (i + 4 <= n && p[i] == 0xF6) {
		if (k < 46)
			dst[k++] = (char)p[i + 2];
		if (k < 46)
			dst[k++] = (char)p[i + 3];
		i += 4;
	}
	while (i + 4 <= n && p[i] == 0xF7) {
		if (k < 46)
			dst[k++] = (char)p[i + 2];
		if (k < 46)
			dst[k++] = (char)p[i + 3];
		i += 4;
	}
	while (k > 0 && (dst[k - 1] == ' ' || dst[k - 1] == 0))
		k--;
	dst[k] = 0;
	return i;
}

static int track_has(const uint8_t *p, int n, int op)
{
	int i;

	for (i = 0; i + 4 <= n; i += 4) {
		if (p[i] == op)
			return 1;
		if (p[i] == 0xFE)
			break;
	}
	return 0;
}

static int track_used(const uint8_t *p, int n)
{
	int i;

	for (i = 0; i + 4 <= n; i += 4) {
		if (p[i] == 0xFE)
			break;
		if (p[i] == 0xF6 || p[i] == 0xF8 || p[i] == 0xF9)
			return 1;
		if (p[i] < 0xF0 && p[i + 1] > 0)
			return 1;
	}
	return 0;
}

/* Comments, and every sounding note is inside a chord. */
static int is_accomp(const uint8_t *p, int n)
{
	int i, grouped = 0, solo = 0;

	if (!track_has(p, n, 0xF6))
		return 0;
	for (i = 0; i + 4 <= n; i += 4) {
		if (p[i] == 0xFE)
			break;
		if (!is_tone(p + i)) {
			if (p[i] < 0x80)
				grouped = 0;
			continue;
		}
		if (p[i + 1] == 0)
			grouped = 1;
		else {
			if (!grouped)
				solo = 1;
			grouped = 0;
		}
	}
	return !solo;
}

static int collect_sym(const uint8_t *p, int n, Sym **out, int *ns)
{
	int i, tick = 0;

	*out = NULL;
	*ns = 0;
	for (i = 0; i + 4 <= n; ) {
		int op = p[i];
		if (op == 0xFE)
			break;
		if (op == 0xF6) {
			Sym *np;
			char text[48];
			i = take_text(p, n, i, text);
			if (text[0] == 0)
				continue;
			np = (Sym *)realloc(*out, (size_t)(*ns + 1) * sizeof(Sym));
			if (np == NULL)
				return -1;
			*out = np;
			(*out)[*ns].tick = tick;
			memcpy((*out)[*ns].text, text, sizeof text);
			(*ns)++;
			continue;
		}
		if (op < 0xF0)
			tick += p[i + 1];
		i += 4;
	}
	return 0;
}

static int flush_sym(Out *o, Sym *sy, int ns, int *si, int tick)
{
	while (*si < ns && sy[*si].tick <= tick) {
		char q[64];
		if (sy[*si].text[0] != 0) {
			snprintf(q, sizeof q, "\"%s\"", sy[*si].text);
			if (out_tok(o, q) != 0)
				return -1;
		}
		(*si)++;
	}
	return 0;
}

static int emit_grp(Out *o, const Grp *g, const int sig[7], int eighth)
{
	char tok[128];
	int i;

	tok[0] = 0;
	if (g->np <= 0) {
		strcpy(tok, "z");
	} else if (g->np == 1) {
		spell(tok, g->pit[0], sig);
	} else {
		char one[32];
		strcpy(tok, "[");
		for (i = 0; i < g->np; i++) {
			spell(one, g->pit[i], sig);
			if (strlen(tok) + strlen(one) + 8 >= sizeof tok)
				break;
			strcat(tok, one);
		}
		strcat(tok, "]");
	}
	add_len(tok, g->step, eighth);
	return out_tok(o, tok);
}

static int emit_program(Out *o, int prog)
{
	char line[96];

	if (prog < 0)
		prog = 0;
	if (prog > 127)
		prog = 127;
	if (o->col > 0 && out_raw(o, "\n") != 0)
		return -1;
	snprintf(line, sizeof line, "%%%%MIDI program %d %% %s\n",
		 prog, gm_program[prog]);
	return out_raw(o, line);
}

static int emit_track(Out *o, const uint8_t *p, int n, const int sig[7],
		      int eighth, Sym *sy, int ns)
{
	int i, tick = 0, si = 0;

	for (i = 0; i + 4 <= n; ) {
		int op = p[i];
		if (op == 0xFE)
			break;
		if (op == 0xFD) {
			int nxt = (i + 4 < n) ? p[i + 4] : 0;
			if (nxt != 0xF9 && out_tok(o, "|") != 0)
				return -1;
			i += 4;
			continue;
		}
		if (op == 0xF9) {
			if (out_tok(o, "|:") != 0)
				return -1;
			i += 4;
			continue;
		}
		if (op == 0xF8) {
			if (p[i + 1] == 2 && out_tok(o, ":|") != 0)
				return -1;
			i += 4;
			continue;
		}
		if (op == 0xEC) {
			int step = p[i + 1];
			if (emit_program(o, p[i + 2]) != 0)
				return -1;
			i += 4;
			if (step > 0) {
				Grp rest;
				memset(&rest, 0, sizeof rest);
				rest.step = step;
				if (emit_grp(o, &rest, sig, eighth) != 0)
					return -1;
				tick += step;
			}
			continue;
		}
		if (op == 0xF6) {
			char text[48];
			char q[64];
			i = take_text(p, n, i, text);
			if (text[0] != 0) {
				snprintf(q, sizeof q, "\"%s\"", text);
				if (out_tok(o, q) != 0)
					return -1;
			}
			continue;
		}
		if (op >= 0x80) {
			i += 4;
			continue;
		}
		if (is_pad(p + i)) {
			i += 4;
			continue;
		}
		{
			Grp g;
			i = read_grp(p, n, i, &g);
			if (g.np > 0)
				i = merge_hold(p, n, i, &g);
			else
				i = merge_rests(p, n, i, &g.step);
			if (flush_sym(o, sy, ns, &si, tick) != 0)
				return -1;
			if (emit_grp(o, &g, sig, eighth) != 0)
				return -1;
			tick += g.step;
		}
	}
	return flush_sym(o, sy, ns, &si, 0x7fffffff);
}

int abc_export_text(const AbcSong *song, char **out)
{
	Out o;
	int sig[7];
	int eighth, i, voice, acc, ntr;
	Sym *sy = NULL;
	int ns = 0;
	char line[96];

	if (out == NULL || song == NULL)
		return -1;
	*out = NULL;
	memset(&o, 0, sizeof o);
	sig_of(song->key, sig);
	eighth = song->timebase > 1 ? song->timebase / 2 : ABC_TIMEBASE / 2;
	ntr = song->ntracks;
	if (ntr > ABC_MAX_TRACKS)
		ntr = ABC_MAX_TRACKS;
	if (ntr < 0)
		ntr = 0;

	if (song->title[0] != 0) {
		snprintf(line, sizeof line, "T:%s\n", song->title);
		if (out_raw(&o, line) != 0)
			goto fail;
	}
	{
		int b1 = song->beat1 > 0 ? song->beat1 : 4;
		int b2 = song->beat2 > 0 ? song->beat2 : 4;
		int tempo = song->tempo > 0 ? song->tempo : 120;
		int key = song->key & 31;
		snprintf(line, sizeof line, "M:%d/%d\nL:1/8\nQ:1/4=%d\nK:%s\n",
			 b1, b2, tempo, key_abc[key]);
		if (out_raw(&o, line) != 0)
			goto fail;
	}

	acc = -1;
	for (i = 0; i < ntr; i++) {
		if (song->trk[i] == NULL)
			continue;
		if (is_accomp(song->trk[i], song->len[i])) {
			acc = i;
			break;
		}
	}
	if (acc >= 0) {
		if (collect_sym(song->trk[acc], song->len[acc], &sy, &ns) != 0)
			goto fail;
	}

	voice = 0;
	for (i = 0; i < ntr; i++) {
		const uint8_t *p;
		if (i == acc || song->trk[i] == NULL)
			continue;
		if (!track_used(song->trk[i], song->len[i]))
			continue;
		voice++;
		snprintf(line, sizeof line, "V:%d\n", voice);
		if (out_raw(&o, line) != 0)
			goto fail;
		p = song->trk[i];
		if (emit_track(&o, p, song->len[i], sig, eighth,
			       voice == 1 ? sy : NULL, voice == 1 ? ns : 0) != 0)
			goto fail;
		if (o.col > 0 && out_raw(&o, "\n") != 0)
			goto fail;
	}
	if (voice == 0 && ns > 0) {
		int si = 0;
		if (out_raw(&o, "V:1\n") != 0)
			goto fail;
		if (flush_sym(&o, sy, ns, &si, 0x7fffffff) != 0)
			goto fail;
		if (o.col > 0 && out_raw(&o, "\n") != 0)
			goto fail;
	}
	free(sy);
	if (o.p == NULL) {
		o.p = (char *)malloc(1);
		if (o.p == NULL)
			return -1;
		o.p[0] = 0;
	}
	*out = o.p;
	return 0;
fail:
	free(sy);
	free(o.p);
	return -1;
}
