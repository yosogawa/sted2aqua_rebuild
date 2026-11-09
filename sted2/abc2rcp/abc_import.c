/*
 * ABC text -> RCP tracks.
 * Timebase is 96. The first tune is read. A second X: stops the parse.
 */
#include "abc2rcp.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_V   ABC_MAX_TRACKS
#define MAX_P   8
#define TB      ABC_TIMEBASE
#define WHOLE   (TB * 4)

enum {
	EK_NOTE = 1,
	EK_REST,
	EK_BAR,
	EK_LS,
	EK_LE,
	EK_VOLTA,
	EK_SYM,
	EK_CH
};

typedef struct Ev {
	int kind;
	int tick;
	int dur;
	int pit[MAX_P];
	int np;
	int tie;
	int vel;
	int gate_dur;
	int count;
	int mid;
	char text[48];
} Ev;

typedef struct Voice {
	Ev *ev;
	int n, cap;
	int sig[7];
	int bar_acc[7];
	int vel;
	int unit_n, unit_d;
	int prog;
	int used;
	int end_tick;
	int pit[MAX_P];
	int np;
	int dur;
	int tie;
	int hold;
	int broken;
	int gate_dur;
	char sym[48];
	int has_sym;
	char incoming[48];
	int has_in;
	char memo[37];
} Voice;

typedef struct Atom {
	int tick;
	int rank;
	int kind;
	int pitch;
	int vel;
	int gate;
	int count;
	char text[48];
} Atom;

enum {
	RK_LE = 0,
	RK_BAR,
	RK_CH,
	RK_PROG,
	RK_TEXT,
	RK_LS,
	RK_NOTE,
	RK_REST
};

static void song_clear(AbcSong *s)
{
	int i;

	memset(s, 0, sizeof *s);
	s->tempo = 120;
	s->beat1 = 4;
	s->beat2 = 4;
	s->timebase = TB;
	for (i = 0; i < ABC_MAX_TRACKS; i++)
		s->mch[i] = 0;
}

void abc_song_free(AbcSong *s)
{
	int i;

	if (s == NULL)
		return;
	for (i = 0; i < ABC_MAX_TRACKS; i++) {
		free(s->trk[i]);
		s->trk[i] = NULL;
		s->len[i] = 0;
	}
}

static void *grow(void *p, int *cap, int need, int sz)
{
	int c;
	void *n;

	if (need <= *cap)
		return p;
	c = *cap > 0 ? *cap : 16;
	while (c < need) {
		if (c > 1000000)
			return NULL;
		c *= 2;
	}
	n = realloc(p, (size_t)c * (size_t)sz);
	if (n == NULL)
		return NULL;
	*cap = c;
	return n;
}

static int push_ev(Voice *v, Ev e)
{
	Ev *n;

	n = grow(v->ev, &v->cap, v->n + 1, (int)sizeof(Ev));
	if (n == NULL)
		return -1;
	v->ev = n;
	v->ev[v->n++] = e;
	v->used = 1;
	return 0;
}

static void reset_bar(Voice *v)
{
	int i;

	for (i = 0; i < 7; i++)
		v->bar_acc[i] = 99;
}

static void set_sig(Voice *v, const int sig[7])
{
	int i;

	for (i = 0; i < 7; i++)
		v->sig[i] = sig[i];
	reset_bar(v);
}

/* A B C D E F G */
static const int k_pc[7] = {9, 11, 0, 2, 4, 5, 7};
static const int k_up[7] = {69, 71, 60, 62, 64, 65, 67};
static const int k_lo[7] = {81, 83, 72, 74, 76, 77, 79};
static const int k_sharp_let[7] = {5, 2, 6, 3, 0, 4, 1}; /* F C G D A E B */
static const int k_flat_let[7] = {1, 4, 0, 3, 6, 2, 5};  /* B E A D G C F */
static const int k_major_sig[7] = {3, 5, 0, 2, 4, -1, 1}; /* A B C D E F G */

static int norm_pc(int pc)
{
	pc %= 12;
	if (pc < 0)
		pc += 12;
	return pc;
}

static void sig_from_n(int n, int sig[7])
{
	int i;

	for (i = 0; i < 7; i++)
		sig[i] = 0;
	if (n > 7)
		n = 7;
	if (n < -7)
		n = -7;
	if (n > 0) {
		for (i = 0; i < n; i++)
			sig[k_sharp_let[i]] = 1;
	} else if (n < 0) {
		for (i = 0; i < -n; i++)
			sig[k_flat_let[i]] = -1;
	}
}

static int bkey_of(int sig, int minor)
{
	if (sig > 7)
		sig = 7;
	if (sig < -7)
		sig = -7;
	if (!minor) {
		if (sig >= 0)
			return sig;
		return 8 + (-sig);
	}
	if (sig >= 0)
		return 16 + sig;
	return 24 + (-sig);
}

static int mode_shift(const char *s)
{
	if (strncmp(s, "mix", 3) == 0)
		return -1;
	if (strncmp(s, "min", 3) == 0 || strcmp(s, "m") == 0 ||
	    strncmp(s, "minor", 5) == 0 || strncmp(s, "aeo", 3) == 0)
		return -3;
	if (strncmp(s, "maj", 3) == 0 || strncmp(s, "ion", 3) == 0)
		return 0;
	if (strncmp(s, "dor", 3) == 0)
		return -2;
	if (strncmp(s, "phr", 3) == 0)
		return -4;
	if (strncmp(s, "lyd", 3) == 0)
		return 1;
	if (strncmp(s, "loc", 3) == 0)
		return -5;
	if (s[0] == 'm' && (s[1] == 0 || s[1] == ' ' ))
		return -3;
	return 0;
}

static int mode_is_minor(const char *s)
{
	if (strncmp(s, "mix", 3) == 0)
		return 0;
	if (strncmp(s, "min", 3) == 0 || strcmp(s, "m") == 0 ||
	    strncmp(s, "minor", 5) == 0 || strncmp(s, "aeo", 3) == 0)
		return 1;
	if (s[0] == 'm' && (s[1] == 0 || s[1] == ' '))
		return 1;
	return 0;
}

/* Returns 0 and fills sig + bkey. */
static int parse_key(const char *field, int sig[7], int *bkey, int *minor_out)
{
	const char *p = field;
	int let, acc, base, sh, minor;
	char mode[32];
	int i;

	while (*p == ' ' || *p == '\t')
		p++;
	if (*p == 0 || strncmp(p, "none", 4) == 0) {
		sig_from_n(0, sig);
		*bkey = 0;
		if (minor_out)
			*minor_out = 0;
		return 0;
	}
	let = -1;
	if (*p >= 'A' && *p <= 'G')
		let = *p - 'A';
	else if (*p >= 'a' && *p <= 'g')
		let = *p - 'a';
	if (let < 0) {
		sig_from_n(0, sig);
		*bkey = 0;
		return 0;
	}
	p++;
	acc = 0;
	if (*p == '#') {
		acc = 1;
		p++;
	} else if (*p == 'b') {
		acc = -1;
		p++;
	}
	while (*p == ' ' || *p == '\t')
		p++;
	i = 0;
	while (*p && *p != ' ' && *p != '\t' && i < 31) {
		mode[i++] = (char)tolower((unsigned char)*p);
		p++;
	}
	mode[i] = 0;
	base = k_major_sig[let] + acc * 7;
	sh = mode_shift(mode);
	minor = mode_is_minor(mode);
	base += sh;
	sig_from_n(base, sig);
	*bkey = bkey_of(base, minor);
	if (minor_out)
		*minor_out = minor;
	return 0;
}

static int parse_meter(const char *field, int *b1, int *b2)
{
	const char *p = field;
	int n = 0, d = 0;

	while (*p == ' ' || *p == '\t')
		p++;
	if (*p == 'C' || *p == 'c') {
		p++;
		if (*p == '|') {
			*b1 = 2;
			*b2 = 2;
		} else {
			*b1 = 4;
			*b2 = 4;
		}
		return 0;
	}
	while (isdigit((unsigned char)*p))
		n = n * 10 + (*p++ - '0');
	if (*p == '/')
		p++;
	while (isdigit((unsigned char)*p))
		d = d * 10 + (*p++ - '0');
	if (n < 1 || d < 2) {
		*b1 = 4;
		*b2 = 4;
		return 0;
	}
	if (n > 32)
		n = 32;
	if (d > 16)
		d = 16;
	*b1 = n;
	*b2 = d;
	return 0;
}

static int parse_tempo(const char *field)
{
	const char *p = field;
	const char *eq = strchr(field, '=');
	int v = 0, any = 0, last = 0;

	if (eq != NULL)
		p = eq + 1;
	while (*p) {
		if (isdigit((unsigned char)*p)) {
			v = 0;
			any = 1;
			while (isdigit((unsigned char)*p))
				v = v * 10 + (*p++ - '0');
			last = v;
		} else {
			p++;
		}
	}
	if (!any)
		return 120;
	if (last < 8)
		last = 8;
	if (last > 255)
		last = 255;
	return last;
}

static int parse_len_field(const char *field, int *n, int *d)
{
	const char *p = field;
	int nn = 0, dd = 0;

	while (*p == ' ' || *p == '\t')
		p++;
	while (isdigit((unsigned char)*p))
		nn = nn * 10 + (*p++ - '0');
	if (*p == '/')
		p++;
	while (isdigit((unsigned char)*p))
		dd = dd * 10 + (*p++ - '0');
	if (nn < 1 || dd < 1) {
		*n = 1;
		*d = 8;
		return 0;
	}
	*n = nn;
	*d = dd;
	return 0;
}

static void copy_title(char *dst, const char *src)
{
	int i = 0;

	while (*src == ' ' || *src == '\t')
		src++;
	while (*src && i < 64) {
		dst[i++] = *src++;
	}
	while (i > 0 && dst[i - 1] == ' ')
		i--;
	dst[i] = 0;
}

static void parse_note_len(const char **pp, int *n, int *d)
{
	const char *p = *pp;
	int nn = 0, dd = 0;

	if (isdigit((unsigned char)*p)) {
		while (isdigit((unsigned char)*p))
			nn = nn * 10 + (*p++ - '0');
	}
	if (*p == '/') {
		p++;
		if (isdigit((unsigned char)*p)) {
			while (isdigit((unsigned char)*p))
				dd = dd * 10 + (*p++ - '0');
		} else {
			dd = 2;
			while (*p == '/') {
				if (dd < 256)
					dd *= 2;
				p++;
			}
		}
	}
	if (nn == 0)
		nn = 1;
	if (dd == 0)
		dd = 1;
	*n = nn;
	*d = dd;
	*pp = p;
}

static int dur_ticks(const Voice *v, int n, int d)
{
	long a, b;

	if (n < 1)
		n = 1;
	if (d < 1)
		d = 1;
	a = (long)WHOLE * v->unit_n * n;
	b = (long)v->unit_d * d;
	if (b <= 0)
		return 0;
	return (int)(a / b);
}

static int scale_broken(int dur, int dots, int first)
{
	int den, num;

	if (dots < 1)
		return dur;
	if (dots > 6)
		dots = 6;
	den = 1 << dots;
	if (first)
		num = (1 << (dots + 1)) - 1;
	else
		num = 1;
	return (int)(((long)dur * num) / den);
}

static int letter_index(int ch)
{
	if (ch >= 'A' && ch <= 'G')
		return ch - 'A';
	if (ch >= 'a' && ch <= 'g')
		return ch - 'a';
	return -1;
}

static int read_pitch2(Voice *v, const char **pp, int *ticks, int *tie)
{
	const char *p = *pp;
	int acc = 0, has = 0, let, lower, midi, commas = 0, quotes = 0;
	int ln, ld;

	if (*p == '^') {
		has = 1;
		while (*p == '^') {
			acc++;
			p++;
		}
	} else if (*p == '_') {
		has = 1;
		while (*p == '_') {
			acc--;
			p++;
		}
	} else if (*p == '=') {
		has = 1;
		acc = 0;
		p++;
	}
	let = letter_index((unsigned char)*p);
	if (let < 0)
		return -1;
	lower = (*p >= 'a' && *p <= 'g');
	p++;
	while (*p == ',') {
		commas++;
		p++;
	}
	while (*p == '\'') {
		quotes++;
		p++;
	}
	parse_note_len(&p, &ln, &ld);
	if (*p == '-') {
		*tie = 1;
		p++;
	} else {
		*tie = 0;
	}
	if (has)
		v->bar_acc[let] = acc;
	else if (v->bar_acc[let] != 99)
		acc = v->bar_acc[let];
	else
		acc = v->sig[let];
	midi = (lower ? k_lo[let] : k_up[let]) + acc;
	midi += 12 * quotes;
	midi -= 12 * commas;
	if (midi < 0)
		midi = 0;
	if (midi > 127)
		midi = 127;
	*ticks = dur_ticks(v, ln, ld);
	*pp = p;
	return midi;
}

static int gate_pct(int dur, int pct)
{
	int g;

	if (dur <= 0)
		return 0;
	g = (int)(((long)dur * pct) / 100);
	if (g < 1)
		return 0;
	return g;
}

static int commit_hold(Voice *v)
{
	Ev e;
	int i;

	if (!v->hold)
		return 0;
	memset(&e, 0, sizeof e);
	if (v->np <= 0 || v->dur <= 0) {
		v->hold = 0;
		v->np = 0;
		v->tie = 0;
		v->has_sym = 0;
		return 0;
	}
	if (v->has_sym) {
		e.kind = EK_SYM;
		snprintf(e.text, sizeof e.text, "%s", v->sym);
		if (push_ev(v, e) != 0)
			return -1;
		v->has_sym = 0;
	}
	memset(&e, 0, sizeof e);
	e.kind = EK_NOTE;
	e.dur = v->dur;
	e.gate_dur = v->gate_dur;
	e.vel = v->vel;
	e.np = v->np;
	e.tie = v->tie;
	for (i = 0; i < v->np; i++)
		e.pit[i] = v->pit[i];
	v->hold = 0;
	v->np = 0;
	v->tie = 0;
	v->broken = 0;
	v->gate_dur = 0;
	return push_ev(v, e);
}

static int start_hold(Voice *v, int *pit, int np, int dur, int tie)
{
	int i;

	if (v->hold && v->broken) {
		int dots = v->broken < 0 ? -v->broken : v->broken;
		int first_long = v->broken > 0;
		v->dur = scale_broken(v->dur, dots, first_long);
		dur = scale_broken(dur, dots, !first_long);
		v->broken = 0;
	}
	if (commit_hold(v) != 0)
		return -1;
	if (np > MAX_P)
		np = MAX_P;
	v->np = np;
	for (i = 0; i < np; i++)
		v->pit[i] = pit[i];
	v->dur = dur;
	v->tie = tie;
	v->gate_dur = 0;
	v->hold = 1;
	if (v->has_in) {
		snprintf(v->sym, sizeof v->sym, "%s", v->incoming);
		v->has_sym = 1;
		v->has_in = 0;
	}
	return 0;
}

static int add_mark(Voice *v, int kind, int count, int mid)
{
	Ev e;

	if (commit_hold(v) != 0)
		return -1;
	v->broken = 0;
	memset(&e, 0, sizeof e);
	e.kind = kind;
	e.count = count;
	e.mid = mid;
	if (kind == EK_BAR || kind == EK_LS || kind == EK_LE)
		reset_bar(v);
	return push_ev(v, e);
}

static int read_volta(const char **pp)
{
	const char *p = *pp;
	int mask = 0;

	if (*p != '[')
		return 0;
	if (!isdigit((unsigned char)p[1]))
		return 0;
	p++;
	while (isdigit((unsigned char)*p)) {
		int n = 0;
		while (isdigit((unsigned char)*p))
			n = n * 10 + (*p++ - '0');
		if (n >= 1 && n <= 8)
			mask |= 1 << (n - 1);
		if (*p == ',' || *p == '-')
			p++;
		else
			break;
	}
	*pp = p;
	return mask;
}

static void read_string(const char **pp, char *dst, int cap)
{
	const char *p = *pp;
	int i = 0;

	if (*p != '"')
		return;
	p++;
	while (*p && *p != '"' && *p != '\n') {
		if (i < cap - 1)
			dst[i++] = *p;
		p++;
	}
	if (*p == '"')
		p++;
	dst[i] = 0;
	*pp = p;
}

static void skip_grace(const char **pp)
{
	const char *p = *pp;

	if (*p != '{')
		return;
	p++;
	while (*p && *p != '}') {
		if (*p == '\n')
			break;
		p++;
	}
	if (*p == '}')
		p++;
	*pp = p;
}

static void skip_bang(const char **pp, Voice *v)
{
	const char *p = *pp;
	char name[16];
	int i = 0;

	if (*p != '!')
		return;
	p++;
	while (*p && *p != '!' && *p != '\n' && i < 15)
		name[i++] = (char)tolower((unsigned char)*p++);
	name[i] = 0;
	if (*p == '!')
		p++;
	*pp = p;
	if (strcmp(name, "pp") == 0)
		v->vel = 40;
	else if (strcmp(name, "p") == 0)
		v->vel = 60;
	else if (strcmp(name, "mp") == 0)
		v->vel = 80;
	else if (strcmp(name, "mf") == 0)
		v->vel = 100;
	else if (strcmp(name, "ff") == 0)
		v->vel = 127;
	else if (strcmp(name, "f") == 0)
		v->vel = 115;
}

typedef struct Parse {
	Voice v[MAX_V];
	char name[MAX_V][32];
	int nv;
	int cur;
	int body;
	int key_set;
	int xcount;
	int bkey;
	int beat1, beat2;
	int tempo;
	int unit_n, unit_d;
	char title[65];
	int err;
	int sig[7];
} Parse;

static Voice *curv(Parse *ps)
{
	if (ps->nv == 0) {
		memset(&ps->v[0], 0, sizeof ps->v[0]);
		ps->v[0].vel = 100;
		ps->v[0].unit_n = ps->unit_n;
		ps->v[0].unit_d = ps->unit_d;
		ps->v[0].prog = -1;
		set_sig(&ps->v[0], ps->sig);
		strcpy(ps->name[0], "1");
		ps->nv = 1;
		ps->cur = 0;
	}
	return &ps->v[ps->cur];
}

/* Keep a 36-byte memo on a UTF-8 boundary. */
static void copy_memo(char *dst, const char *src)
{
	int n = 0;

	while (src[n] != 0 && n < 36)
		n++;
	while (n > 0 && ((unsigned char)src[n - 1] & 0xc0) == 0x80)
		n--;
	if (n > 0 && ((unsigned char)src[n - 1] & 0xc0) == 0xc0)
		n--;
	memcpy(dst, src, (size_t)n);
	dst[n] = 0;
}

/* V: id name="Drums" snm="Dr." clef=perc. Only name= becomes the memo. */
static void take_voice_name(Voice *v, const char *field)
{
	const char *p = field;

	while (*p) {
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == 0)
			break;
		if (strncmp(p, "name=", 5) == 0) {
			char buf[128];
			int n = 0;
			p += 5;
			if (*p == '"') {
				p++;
				while (*p && *p != '"' && *p != '\n' && n < 120) {
					if (*p == '\\' && p[1] != 0)
						p++;
					buf[n++] = *p++;
				}
				if (*p == '"')
					p++;
			} else {
				while (*p && *p != ' ' && *p != '\t' &&
				       *p != '\n' && n < 120)
					buf[n++] = *p++;
			}
			buf[n] = 0;
			copy_memo(v->memo, buf);
			continue;
		}
		if (*p == '"') {
			p++;
			while (*p && *p != '"') {
				if (*p == '\\' && p[1] != 0)
					p++;
				p++;
			}
			if (*p == '"')
				p++;
		} else {
			while (*p && *p != ' ' && *p != '\t')
				p++;
		}
	}
}

static int switch_voice(Parse *ps, const char *field)
{
	char id[32];
	int i = 0, k;
	const char *p = field;
	Voice *v;

	while (*p == ' ' || *p == '\t')
		p++;
	while (*p && *p != ' ' && *p != '\t' && i < 31)
		id[i++] = *p++;
	id[i] = 0;
	if (id[0] == 0)
		strcpy(id, "1");
	for (k = 0; k < ps->nv; k++) {
		if (strcmp(ps->name[k], id) == 0) {
			ps->cur = k;
			take_voice_name(&ps->v[k], p);
			return 0;
		}
	}
	if (ps->nv >= MAX_V)
		return 0;
	k = ps->nv++;
	memset(&ps->v[k], 0, sizeof ps->v[k]);
	ps->v[k].vel = 100;
	ps->v[k].unit_n = ps->unit_n;
	ps->v[k].unit_d = ps->unit_d;
	ps->v[k].prog = -1;
	set_sig(&ps->v[k], ps->sig);
	snprintf(ps->name[k], sizeof ps->name[k], "%s", id);
	ps->cur = k;
	v = &ps->v[k];
	take_voice_name(v, p);
	return 0;
}

static void apply_key_field(Parse *ps, const char *field)
{
	int sig[7], bkey = 0;
	int i;

	parse_key(field, sig, &bkey, NULL);
	for (i = 0; i < 7; i++)
		ps->sig[i] = sig[i];
	if (!ps->key_set) {
		ps->bkey = bkey;
		ps->key_set = 1;
	}
	if (ps->nv == 0)
		curv(ps);
	for (i = 0; i < ps->nv; i++)
		set_sig(&ps->v[i], ps->sig);
	ps->body = 1;
}

static void handle_field(Parse *ps, char letter, const char *val)
{
	letter = (char)toupper((unsigned char)letter);
	if (letter == 'X') {
		ps->xcount++;
		return;
	}
	if (letter == 'T' && ps->title[0] == 0)
		copy_title(ps->title, val);
	if (!ps->body) {
		if (letter == 'M')
			parse_meter(val, &ps->beat1, &ps->beat2);
		if (letter == 'Q')
			ps->tempo = parse_tempo(val);
		if (letter == 'L')
			parse_len_field(val, &ps->unit_n, &ps->unit_d);
	}
	if (letter == 'L') {
		parse_len_field(val, &ps->unit_n, &ps->unit_d);
		if (ps->nv > 0) {
			ps->v[ps->cur].unit_n = ps->unit_n;
			ps->v[ps->cur].unit_d = ps->unit_d;
		}
	}
	if (letter == 'K')
		apply_key_field(ps, val);
	if (letter == 'V')
		switch_voice(ps, val);
	if (letter == 'P' || letter == 'W' || letter == 'N' || letter == 'C')
		return;
}

/*
 * ABC Transcription Tools plays programs above 127 itself, and remaps
 * them to General MIDI only when writing a MIDI file. STed2 stores a
 * GM program, so the import uses that same remap. 128 is percussion.
 * Any other number above 127 sets no program.
 * Returns the GM program, or -1 when the directive should be ignored.
 */
static int gm_program_fallback(int prog)
{
	if (prog >= 0 && prog <= 127)
		return prog;
	switch (prog) {
	case 129: case 130: case 131: case 132: case 143:
		return 109; /* pipes -> Bagpipe */
	case 133: case 134:
		return 22; /* Concertina, Melodica -> Harmonica */
	case 135:
		return 21; /* Cajun Accordion */
	case 136:
		return 52; /* Solfege -> Choir Aahs */
	case 137:
		return 24; /* Chorused Guitar (nylon) */
	case 138: case 139: case 140: case 141:
		return 25; /* steel guitar, bouzouki, mandolin */
	case 142:
		return 117; /* Marching Drums -> Melodic Tom */
	case 144: case 145: case 146: case 147:
		return 74; /* recorders */
	case 148: case 149:
		return 15; /* Mountain Dulcimer */
	default:
		return -1;
	}
}

static int midi_program_line(const char *line, int *prog)
{
	const char *p = line;
	int v = 0;

	while (*p == ' ' || *p == '\t')
		p++;
	if (strncmp(p, "%%MIDI", 6) != 0)
		return 0;
	p += 6;
	while (*p == ' ' || *p == '\t')
		p++;
	if (strncmp(p, "program", 7) != 0)
		return 0;
	p += 7;
	while (*p == ' ' || *p == '\t')
		p++;
	if (!isdigit((unsigned char)*p))
		return 0;
	while (isdigit((unsigned char)*p)) {
		if (v > 1000000)
			v = 1000001;
		else
			v = v * 10 + (*p - '0');
		p++;
	}
	*prog = v;
	return 1;
}

/* %%MIDI channel n, n = 1..16 (A1..A16). Other numbers are ignored. */
static int midi_channel_line(const char *line, int *ch)
{
	const char *p = line;
	int v = 0;

	*ch = -1;
	while (*p == ' ' || *p == '\t')
		p++;
	if (strncmp(p, "%%MIDI", 6) != 0)
		return 0;
	p += 6;
	while (*p == ' ' || *p == '\t')
		p++;
	if (strncmp(p, "channel", 7) != 0)
		return 0;
	p += 7;
	while (*p == ' ' || *p == '\t')
		p++;
	if (!isdigit((unsigned char)*p))
		return 0;
	while (isdigit((unsigned char)*p)) {
		if (v > 1000000)
			v = 1000001;
		else
			v = v * 10 + (*p - '0');
		p++;
	}
	if (v >= 1 && v <= 16)
		*ch = v;
	return 1;
}

static int take_chord(Voice *v, const char **pp)
{
	const char *p = *pp;
	int pit[MAX_P], tie[MAX_P], np = 0, ticks = 0, any_tie = 0;
	int i;

	if (*p != '[')
		return 0;
	if (p[1] == '|')
		return 0;
	if (isalpha((unsigned char)p[1]) && p[2] == ':')
		return 0;
	p++;
	while (*p && *p != ']') {
		int t = 0, ti = 0, m;
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == ']' || *p == 0)
			break;
		m = read_pitch2(v, &p, &t, &ti);
		if (m < 0)
			break;
		if (np < MAX_P) {
			pit[np] = m;
			tie[np] = ti;
			if (t > ticks)
				ticks = t;
			np++;
		}
		if (ti)
			any_tie = 1;
	}
	if (*p == ']')
		p++;
	{
		int ln, ld;
		parse_note_len(&p, &ln, &ld);
		if (!(ln == 1 && ld == 1))
			ticks = dur_ticks(v, ln, ld);
	}
	if (*p == '-') {
		any_tie = 1;
		for (i = 0; i < np; i++)
			tie[i] = 1;
		p++;
	}
	*pp = p;
	if (np == 0)
		return 0;
	{
		int mask = 0;
		for (i = 0; i < np; i++) {
			if (tie[i])
				mask |= 1 << i;
		}
		if (any_tie && mask == 0)
			mask = (1 << np) - 1;
		if (start_hold(v, pit, np, ticks, mask) != 0)
			v->hold = 0;
	}
	return 1;
}

static int parse_body_char(Parse *ps, const char **pp, int *at_line)
{
	const char *p = *pp;
	Voice *v;
	int ch;

	v = curv(ps);
	ch = (unsigned char)*p;
	if (ch == 0)
		return 0;
	if (ch == '\n') {
		*at_line = 1;
		*pp = p + 1;
		return 1;
	}
	if (ch == '\r' || ch == ' ' || ch == '\t') {
		*pp = p + 1;
		return 1;
	}
	if (ch == '\\' && (p[1] == '\n' || p[1] == '\r')) {
		*pp = p + 1;
		return 1;
	}
	if (ch == '%') {
		if (*at_line && p[1] == '%') {
			int prog = 0, chn = -1;
			if (midi_channel_line(p, &chn)) {
				if (chn >= 1 && chn <= 16) {
					Ev e;
					v = curv(ps);
					if (commit_hold(v) != 0) {
						ps->err = 1;
						return 0;
					}
					memset(&e, 0, sizeof e);
					e.kind = EK_CH;
					e.count = chn;
					if (push_ev(v, e) != 0) {
						ps->err = 1;
						return 0;
					}
				}
			} else if (midi_program_line(p, &prog)) {
				int gm = gm_program_fallback(prog);
				if (gm >= 0) {
					v = curv(ps);
					v->prog = gm;
				}
			}
		}
		while (*p && *p != '\n')
			p++;
		*pp = p;
		return 1;
	}
	if (*at_line && isalpha(ch) && p[1] == ':') {
		char letter = (char)ch;
		char val[256];
		int i = 0;
		p += 2;
		while (*p && *p != '\n' && *p != '\r' && i < 255)
			val[i++] = *p++;
		val[i] = 0;
		if (letter == 'X' || letter == 'x') {
			if (ps->xcount >= 1) {
				*pp = p;
				return 0;
			}
		}
		if (letter == 'w' || letter == 'W') {
			*pp = p;
			*at_line = 0;
			return 1;
		}
		handle_field(ps, letter, val);
		*pp = p;
		*at_line = 0;
		if (ps->xcount >= 2)
			return 0;
		return 1;
	}
	*at_line = 0;
	ps->body = 1;
	v = curv(ps);
	v->unit_n = ps->unit_n;
	v->unit_d = ps->unit_d;

	if (ch == '!') {
		skip_bang(&p, v);
		*pp = p;
		return 1;
	}
	if (ch == '{') {
		skip_grace(&p);
		*pp = p;
		return 1;
	}
	if (ch == '"') {
		read_string(&p, v->incoming, (int)sizeof v->incoming);
		v->has_in = v->incoming[0] != 0;
		*pp = p;
		return 1;
	}
	if (ch == '(' || ch == ')' || ch == '~' || ch == '.') {
		/* Slur, roll, and tuplet marks. The notes keep their lengths. */
		p++;
		if (ch == '(' && isdigit((unsigned char)*p)) {
			while (isdigit((unsigned char)*p))
				p++;
			if (*p == ':') {
				p++;
				while (isdigit((unsigned char)*p) || *p == ':')
					p++;
			}
		}
		*pp = p;
		return 1;
	}
	if (ch == '>') {
		int n = 0;
		while (*p == '>') {
			n++;
			p++;
		}
		if (v->hold)
			v->broken = n;
		*pp = p;
		return 1;
	}
	if (ch == '<') {
		int n = 0;
		while (*p == '<') {
			n++;
			p++;
		}
		if (v->hold)
			v->broken = -n;
		*pp = p;
		return 1;
	}
	if (ch == '|') {
		p++;
		if (commit_hold(v) != 0) {
			ps->err = 1;
			return 0;
		}
		if (*p == ':') {
			p++;
			if (add_mark(v, EK_BAR, 0, 0) != 0 ||
			    add_mark(v, EK_LS, 0, 0) != 0) {
				ps->err = 1;
				return 0;
			}
		} else {
			if (*p == ']')
				p++;
			if (add_mark(v, EK_BAR, 0, 0) != 0) {
				ps->err = 1;
				return 0;
			}
		}
		*pp = p;
		return 1;
	}
	if (ch == ':') {
		int count = 2;
		if (p[1] == '|') {
			p += 2;
			if (isdigit((unsigned char)*p)) {
				count = 0;
				while (isdigit((unsigned char)*p))
					count = count * 10 + (*p++ - '0');
				if (count < 1)
					count = 2;
			}
			if (commit_hold(v) != 0) {
				ps->err = 1;
				return 0;
			}
			if (add_mark(v, EK_LE, count, 0) != 0 ||
			    add_mark(v, EK_BAR, 0, 0) != 0) {
				ps->err = 1;
				return 0;
			}
			if (*p == ':') {
				p++;
				if (add_mark(v, EK_LS, 0, 0) != 0) {
					ps->err = 1;
					return 0;
				}
			}
			*pp = p;
			return 1;
		}
		if (p[1] == ':') {
			p += 2;
			if (commit_hold(v) != 0) {
				ps->err = 1;
				return 0;
			}
			if (add_mark(v, EK_LE, 2, 0) != 0 ||
			    add_mark(v, EK_BAR, 0, 0) != 0 ||
			    add_mark(v, EK_LS, 0, 0) != 0) {
				ps->err = 1;
				return 0;
			}
			*pp = p;
			return 1;
		}
		*pp = p + 1;
		return 1;
	}
	if (ch == '[') {
		int mask;
		if (p[1] == '|') {
			p += 2;
			if (commit_hold(v) != 0) {
				ps->err = 1;
				return 0;
			}
			if (*p == ':') {
				p++;
				if (add_mark(v, EK_BAR, 0, 0) != 0 ||
				    add_mark(v, EK_LS, 0, 0) != 0) {
					ps->err = 1;
					return 0;
				}
			} else if (add_mark(v, EK_BAR, 0, 0) != 0) {
				ps->err = 1;
				return 0;
			}
			*pp = p;
			return 1;
		}
		mask = read_volta(&p);
		if (mask) {
			if (commit_hold(v) != 0) {
				ps->err = 1;
				return 0;
			}
			if (add_mark(v, EK_VOLTA, mask, 0) != 0) {
				ps->err = 1;
				return 0;
			}
			*pp = p;
			return 1;
		}
		if (isalpha((unsigned char)p[1]) && p[2] == ':') {
			char letter = p[1];
			char val[256];
			int i = 0;
			p += 3;
			while (*p && *p != ']' && *p != '\n' && i < 255)
				val[i++] = *p++;
			val[i] = 0;
			if (*p == ']')
				p++;
			handle_field(ps, letter, val);
			*pp = p;
			return 1;
		}
		if (take_chord(v, &p)) {
			*pp = p;
			return 1;
		}
		*pp = p + 1;
		return 1;
	}
	if (ch == ']' ) {
		*pp = p + 1;
		return 1;
	}
	if (ch == 'z' || ch == 'x' || ch == 'Z' || ch == 'y') {
		int ln, ld, ticks, dots;
		p++;
		parse_note_len(&p, &ln, &ld);
		ticks = (ch == 'y') ? 0 : dur_ticks(v, ln, ld);
		if (ch != 'y') {
			if (v->hold && v->broken) {
				dots = v->broken < 0 ? -v->broken : v->broken;
				v->dur = scale_broken(v->dur, dots, v->broken > 0);
				ticks = scale_broken(ticks, dots, v->broken < 0);
				v->broken = 0;
			}
			if (commit_hold(v) != 0) {
				ps->err = 1;
				return 0;
			}
			if (v->has_in && ticks > 0) {
				Ev s;
				memset(&s, 0, sizeof s);
				s.kind = EK_SYM;
				snprintf(s.text, sizeof s.text, "%s", v->incoming);
				v->has_in = 0;
				if (push_ev(v, s) != 0) {
					ps->err = 1;
					return 0;
				}
			}
			if (ticks > 0) {
				Ev e;
				memset(&e, 0, sizeof e);
				e.kind = EK_REST;
				e.dur = ticks;
				if (push_ev(v, e) != 0) {
					ps->err = 1;
					return 0;
				}
			}
		}
		*pp = p;
		return 1;
	}
	if (ch == '^' || ch == '_' || ch == '=' ||
	    (ch >= 'A' && ch <= 'G') || (ch >= 'a' && ch <= 'g')) {
		int midi, ticks = 0, ti = 0;
		midi = read_pitch2(v, &p, &ticks, &ti);
		if (midi >= 0) {
			int pit = midi;
			int mask = ti ? 1 : 0;
			if (start_hold(v, &pit, 1, ticks, mask) != 0) {
				ps->err = 1;
				return 0;
			}
		}
		*pp = p;
		return 1;
	}
	/* Unknown decoration or letter. */
	*pp = p + 1;
	return 1;
}

static void assign_ticks(Voice *v)
{
	int i, t = 0;

	for (i = 0; i < v->n; i++) {
		v->ev[i].tick = t;
		t += v->ev[i].dur;
	}
	v->end_tick = t;
}

static int span_has_le(const Ev *ev, int a, int b)
{
	int i;

	for (i = a; i < b; i++) {
		if (ev[i].kind == EK_LE)
			return 1;
	}
	return 0;
}

static int find_ls(const Ev *ev, int end, int *start)
{
	int i, depth = 0;

	for (i = end - 1; i >= 0; i--) {
		if (ev[i].kind == EK_LE)
			depth++;
		else if (ev[i].kind == EK_LS) {
			if (depth == 0) {
				*start = i;
				return 1;
			}
			depth--;
		}
	}
	return 0;
}

static int volta_ok(int mask, int pass)
{
	if (mask == 0)
		return 1;
	if (pass < 1 || pass > 8)
		return 0;
	return (mask & (1 << (pass - 1))) != 0;
}

static int append_ev(Ev **dst, int *n, int *cap, Ev e)
{
	Ev *p;

	p = grow(*dst, cap, *n + 1, (int)sizeof(Ev));
	if (p == NULL)
		return -1;
	*dst = p;
	(*dst)[(*n)++] = e;
	return 0;
}

static int copy_pass(const Ev *ev, int a, int b, int pass, Ev **dst, int *n, int *cap)
{
	int i, mask = 0;

	for (i = a; i < b; i++) {
		if (ev[i].kind == EK_VOLTA) {
			mask = ev[i].count;
			continue;
		}
		if (ev[i].kind == EK_LS || ev[i].kind == EK_LE)
			continue;
		if (!volta_ok(mask, pass))
			continue;
		if (append_ev(dst, n, cap, ev[i]) != 0)
			return -1;
	}
	return 0;
}

/* Expand the innermost loop once. Missing |: starts at the beginning. */
static int expand_once(Voice *v)
{
	int i, start, count, tail, pass;
	Ev *neu = NULL;
	int nn = 0, cap = 0;

	for (i = 0; i < v->n; i++) {
		if (v->ev[i].kind != EK_LE)
			continue;
		if (!find_ls(v->ev, i, &start))
			start = -1;
		if (span_has_le(v->ev, start + 1, i))
			continue;
		count = v->ev[i].count;
		if (count < 1)
			count = 2;
		if (count > 8)
			count = 8;
		tail = i + 1;
		if (tail < v->n && v->ev[tail].kind == EK_VOLTA) {
			while (tail < v->n && v->ev[tail].kind != EK_LS &&
			       v->ev[tail].kind != EK_LE)
				tail++;
		}
		if (start > 0) {
			int k;
			for (k = 0; k < start; k++) {
				if (append_ev(&neu, &nn, &cap, v->ev[k]) != 0)
					goto fail;
			}
		}
		for (pass = 1; pass <= count; pass++) {
			if (copy_pass(v->ev, start + 1, i, pass, &neu, &nn, &cap) != 0)
				goto fail;
			if (pass == count && i + 1 < tail) {
				if (copy_pass(v->ev, i + 1, tail, pass, &neu, &nn, &cap) != 0)
					goto fail;
			}
		}
		for (pass = tail; pass < v->n; pass++) {
			if (append_ev(&neu, &nn, &cap, v->ev[pass]) != 0)
				goto fail;
		}
		free(v->ev);
		v->ev = neu;
		v->n = nn;
		v->cap = cap;
		return 1;
	}
	return 0;
fail:
	free(neu);
	return -1;
}

static int voice_needs_expand(const Voice *v)
{
	int i, open = 0;

	for (i = 0; i < v->n; i++) {
		if (v->ev[i].kind == EK_LS)
			open++;
		if (v->ev[i].kind == EK_VOLTA)
			return 1;
		if (v->ev[i].kind == EK_LE) {
			if (v->ev[i].count != 2 || v->ev[i].mid)
				return 1;
			if (open <= 0)
				return 1;
			if (open > 1)
				return 1;
			open--;
		}
		if (v->ev[i].kind == EK_LS && v->ev[i].mid)
			return 1;
	}
	if (open != 0)
		return 1;
	return 0;
}

static int collect_bars(const Voice *v, int **out, int *nout)
{
	int i, n = 0, cap = 0;
	int *a = NULL;

	for (i = 0; i < v->n; i++) {
		if (v->ev[i].kind == EK_BAR || v->ev[i].kind == EK_LS ||
		    v->ev[i].kind == EK_LE) {
			int *p = grow(a, &cap, n + 1, (int)sizeof(int));
			if (p == NULL) {
				free(a);
				return -1;
			}
			a = p;
			a[n++] = v->ev[i].tick;
		}
	}
	*out = a;
	*nout = n;
	return 0;
}

static int bars_equal(const int *a, int na, const int *b, int nb)
{
	int i;

	if (na != nb)
		return 0;
	for (i = 0; i < na; i++) {
		if (a[i] != b[i])
			return 0;
	}
	return 1;
}

static int resolve_repeats(Parse *ps)
{
	int i, expand = 0;
	int *base = NULL, nb = 0, have = 0;

	for (i = 0; i < ps->nv; i++)
		assign_ticks(&ps->v[i]);
	for (i = 0; i < ps->nv; i++) {
		int *b = NULL, n = 0;
		if (!ps->v[i].used)
			continue;
		if (voice_needs_expand(&ps->v[i]))
			expand = 1;
		if (collect_bars(&ps->v[i], &b, &n) != 0)
			return -1;
		if (!have) {
			base = b;
			nb = n;
			have = 1;
		} else {
			if (!bars_equal(base, nb, b, n))
				expand = 1;
			free(b);
		}
	}
	free(base);
	if (!expand)
		return 0;
	for (i = 0; i < ps->nv; i++) {
		int guard = 0;
		while (guard < 64) {
			int r = expand_once(&ps->v[i]);
			if (r == 0)
				break;
			if (r < 0)
				return -1;
			guard++;
		}
		assign_ticks(&ps->v[i]);
	}
	return 0;
}

static int tie_merge(Voice *v)
{
	int i;

	for (i = 0; i < v->n; i++) {
		int j, bit;
		if (v->ev[i].kind != EK_NOTE || v->ev[i].tie == 0)
			continue;
		j = i + 1;
		while (j < v->n && v->ev[j].dur == 0 && v->ev[j].kind != EK_NOTE &&
		       v->ev[j].kind != EK_REST)
			j++;
		if (j >= v->n || v->ev[j].kind != EK_NOTE)
			continue;
		for (bit = 0; bit < v->ev[i].np; bit++) {
			int k, full;
			if ((v->ev[i].tie & (1 << bit)) == 0)
				continue;
			for (k = 0; k < v->ev[j].np; k++) {
				if (v->ev[j].pit[k] != v->ev[i].pit[bit])
					continue;
				full = (v->ev[i].np == 1 && v->ev[j].np == 1);
				if (full) {
					v->ev[i].dur += v->ev[j].dur;
					v->ev[j].dur = 0;
					v->ev[j].np = 0;
					v->ev[j].kind = EK_REST;
				} else if (v->ev[i].gate_dur < v->ev[i].dur) {
					v->ev[i].gate_dur = v->ev[i].dur + v->ev[j].dur;
				} else {
					v->ev[i].gate_dur += v->ev[j].dur;
				}
				v->ev[i].tie &= ~(1 << bit);
				if (v->ev[j].tie & (1 << k))
					v->ev[i].tie |= 1 << bit;
				{
					int t;
					for (t = k; t < v->ev[j].np - 1; t++) {
						v->ev[j].pit[t] = v->ev[j].pit[t + 1];
						if (v->ev[j].tie & (1 << (t + 1)))
							v->ev[j].tie |= 1 << t;
						else
							v->ev[j].tie &= ~(1 << t);
					}
					if (v->ev[j].np > 0)
						v->ev[j].np--;
				}
				break;
			}
		}
		if (v->ev[j].np <= 0) {
			v->ev[j].kind = EK_REST;
			if (v->ev[i].dur > v->ev[j].dur && v->ev[j].np == 0 &&
			    (v->ev[i].tie == 0)) {
				/* A fully consumed chord leaves no second attack.
				 * Its time already moved into the first event only
				 * when both sides were single notes. */
			}
		}
	}
	return 0;
}

typedef struct Buf {
	uint8_t *p;
	int n, cap;
} Buf;

static int buf_byte(Buf *b, int a, int s, int c, int d)
{
	uint8_t *n;

	if (s < 0)
		s = 0;
	if (s > 255)
		s = 255;
	if (c < 0)
		c = 0;
	if (c > 255)
		c = 255;
	if (d < 0)
		d = 0;
	if (d > 255)
		d = 255;
	n = grow(b->p, &b->cap, b->n + 4, 1);
	if (n == NULL)
		return -1;
	b->p = n;
	b->p[b->n++] = (uint8_t)a;
	b->p[b->n++] = (uint8_t)s;
	b->p[b->n++] = (uint8_t)c;
	b->p[b->n++] = (uint8_t)d;
	return 0;
}

static int buf_text(Buf *b, const char *text)
{
	char tmp[48];
	int len, i, first = 1;

	snprintf(tmp, sizeof tmp, "%s", text);
	len = (int)strlen(tmp);
	if (len == 0)
		return 0;
	if (len & 1) {
		if (len < (int)sizeof tmp - 1) {
			tmp[len++] = ' ';
			tmp[len] = 0;
		}
	}
	for (i = 0; i + 1 < len; i += 2) {
		int op = first ? 0xF6 : 0xF7;
		first = 0;
		if (buf_byte(b, op, 0, (unsigned char)tmp[i], (unsigned char)tmp[i + 1]) != 0)
			return -1;
	}
	return 0;
}

static int atom_push(Atom **a, int *n, int *cap, Atom e)
{
	Atom *p = grow(*a, cap, *n + 1, (int)sizeof(Atom));

	if (p == NULL)
		return -1;
	*a = p;
	(*a)[(*n)++] = e;
	return 0;
}

static int cmp_atom(const void *xa, const void *xb)
{
	const Atom *a = xa, *b = xb;
	if (a->tick != b->tick)
		return a->tick - b->tick;
	if (a->rank != b->rank)
		return a->rank - b->rank;
	return 0;
}

static int voice_atoms(Voice *v, Atom **out, int *nout, int *end_tick,
		      int comments)
{
	int i;
	Atom *a = NULL;
	int n = 0, cap = 0;

	tie_merge(v);
	assign_ticks(v);
	if (v->prog >= 0) {
		Atom e;
		memset(&e, 0, sizeof e);
		e.kind = RK_PROG;
		e.rank = RK_PROG;
		e.count = v->prog;
		if (atom_push(&a, &n, &cap, e) != 0)
			return -1;
	}
	for (i = 0; i < v->n; i++) {
		Atom e;
		int k;
		memset(&e, 0, sizeof e);
		e.tick = v->ev[i].tick;
		if (v->ev[i].kind == EK_BAR) {
			e.kind = RK_BAR;
			e.rank = RK_BAR;
			if (atom_push(&a, &n, &cap, e) != 0)
				goto fail;
		} else if (v->ev[i].kind == EK_LS) {
			e.kind = RK_LS;
			e.rank = RK_LS;
			if (atom_push(&a, &n, &cap, e) != 0)
				goto fail;
		} else if (v->ev[i].kind == EK_LE) {
			e.kind = RK_LE;
			e.rank = RK_LE;
			e.count = v->ev[i].count > 0 ? v->ev[i].count : 2;
			if (atom_push(&a, &n, &cap, e) != 0)
				goto fail;
		} else if (v->ev[i].kind == EK_NOTE) {
			int base = v->ev[i].dur;
			int g;
			if (v->ev[i].gate_dur > base)
				base = v->ev[i].gate_dur;
			g = gate_pct(base, 80);
			if (g < 1)
				continue;
			for (k = 0; k < v->ev[i].np; k++) {
				memset(&e, 0, sizeof e);
				e.tick = v->ev[i].tick;
				e.kind = RK_NOTE;
				e.rank = RK_NOTE;
				e.pitch = v->ev[i].pit[k];
				e.vel = v->ev[i].vel;
				e.gate = g;
				if (atom_push(&a, &n, &cap, e) != 0)
					goto fail;
			}
		} else if (v->ev[i].kind == EK_REST && v->ev[i].dur > 0) {
			e.kind = RK_REST;
			e.rank = RK_REST;
			if (atom_push(&a, &n, &cap, e) != 0)
				goto fail;
		} else if (v->ev[i].kind == EK_CH &&
			   v->ev[i].count >= 1 && v->ev[i].count <= 16) {
			e.kind = RK_CH;
			e.rank = RK_CH;
			e.count = v->ev[i].count;
			if (atom_push(&a, &n, &cap, e) != 0)
				goto fail;
		} else if (comments && v->ev[i].kind == EK_SYM &&
			   v->ev[i].text[0] != 0) {
			e.kind = RK_TEXT;
			e.rank = RK_TEXT;
			snprintf(e.text, sizeof e.text, "%s", v->ev[i].text);
			if (atom_push(&a, &n, &cap, e) != 0)
				goto fail;
		}
	}
	qsort(a, (size_t)n, sizeof(Atom), cmp_atom);
	*out = a;
	*nout = n;
	*end_tick = v->end_tick;
	return 0;
fail:
	free(a);
	return -1;
}

static int add_split(int **s, int *n, int *cap, int t)
{
	int i;
	int *p;

	if (t < 0)
		t = 0;
	for (i = 0; i < *n; i++) {
		if ((*s)[i] == t)
			return 0;
	}
	p = grow(*s, cap, *n + 1, (int)sizeof(int));
	if (p == NULL)
		return -1;
	*s = p;
	(*s)[(*n)++] = t;
	return 0;
}

static int cmp_int(const void *a, const void *b)
{
	return *(const int *)a - *(const int *)b;
}

typedef struct Hold {
	int pitch;
	int vel;
	int gate_end;
	int on;
} Hold;

static int emit_atoms(Atom *a, int na, int end_tick, Buf *b)
{
	int *sp = NULL, ns = 0, cs = 0;
	int i, t;
	Hold hold[16];
	int nh = 0;

	if (end_tick < 0)
		end_tick = 0;
	if (add_split(&sp, &ns, &cs, 0) != 0 ||
	    add_split(&sp, &ns, &cs, end_tick) != 0)
		goto fail;
	for (i = 0; i < na; i++) {
		if (add_split(&sp, &ns, &cs, a[i].tick) != 0)
			goto fail;
	}
	for (t = 240; t < end_tick; t += 240) {
		if (add_split(&sp, &ns, &cs, t) != 0)
			goto fail;
	}
	qsort(sp, (size_t)ns, sizeof(int), cmp_int);
	memset(hold, 0, sizeof hold);
	for (i = 0; i + 1 < ns; i++) {
		int t0 = sp[i], t1 = sp[i + 1], st = t1 - t0;
		int evs[24], ne = 0;
		int k, h;
		if (st < 0)
			st = 0;
		if (st > 255)
			st = 255;
		for (k = 0; k < na; k++) {
			if (a[k].tick != t0)
				continue;
			if (a[k].kind == RK_BAR) {
				if (ne < 24)
					evs[ne++] = k;
			} else if (a[k].kind == RK_LE || a[k].kind == RK_LS ||
				   a[k].kind == RK_PROG || a[k].kind == RK_CH ||
				   a[k].kind == RK_TEXT) {
				if (ne < 24)
					evs[ne++] = k;
			} else if (a[k].kind == RK_NOTE) {
				int found = -1;
				for (h = 0; h < nh; h++) {
					if (hold[h].on && hold[h].pitch == a[k].pitch) {
						found = h;
						break;
					}
				}
				if (found < 0 && nh < 16) {
					found = nh++;
					hold[found].on = 1;
					hold[found].pitch = a[k].pitch;
				}
				if (found >= 0) {
					hold[found].vel = a[k].vel;
					hold[found].gate_end = t0 + a[k].gate;
					hold[found].on = 1;
				}
				if (ne < 24)
					evs[ne++] = k;
			}
		}
		/* Refresh notes that would expire during this slice. */
		for (h = 0; h < nh; h++) {
			int left, already = 0;
			if (!hold[h].on)
				continue;
			left = hold[h].gate_end - t0;
			if (left <= 0) {
				hold[h].on = 0;
				continue;
			}
			for (k = 0; k < ne; k++) {
				if (a[evs[k]].kind == RK_NOTE &&
				    a[evs[k]].pitch == hold[h].pitch)
					already = 1;
			}
			if (!already && left > 0 && (left <= st || st >= 240)) {
				/* Synthesize a refresh by reusing a note atom index
				 * of -1. Emit directly after markers. */
			}
		}
		{
			int emitted_note = 0;
			int note_at[16], nn = 0;
			for (k = 0; k < ne; k++) {
				if (a[evs[k]].kind != RK_NOTE &&
				    a[evs[k]].kind != RK_TEXT &&
				    a[evs[k]].kind != RK_BAR &&
				    a[evs[k]].kind != RK_LS &&
				    a[evs[k]].kind != RK_LE &&
				    a[evs[k]].kind != RK_PROG)
					continue;
			}
			for (k = 0; k < ne; k++) {
				Atom *e = &a[evs[k]];
				int step = 0;
				int gate = 0;
				if (e->kind == RK_NOTE) {
					int left = e->gate;
					if (left > 255)
						left = 255;
					if (nn < 16)
						note_at[nn++] = evs[k];
					(void)step;
					(void)gate;
					(void)left;
					emitted_note = 1;
				}
			}
			/* Markers first, then notes. Last carrier gets st. */
			for (k = 0; k < ne; k++) {
				Atom *e = &a[evs[k]];
				if (e->kind == RK_BAR) {
					if (buf_byte(b, 0xFD, 0, 0, 0) != 0)
						goto fail;
				} else if (e->kind == RK_LE) {
					int c = e->count;
					if (c < 1)
						c = 2;
					if (c > 255)
						c = 255;
					if (buf_byte(b, 0xF8, 0, 0, 0) != 0)
						goto fail;
					b->p[b->n - 3] = (uint8_t)c;
				} else if (e->kind == RK_LS) {
					if (buf_byte(b, 0xF9, 0, 0, 0) != 0)
						goto fail;
				} else if (e->kind == RK_CH) {
					int c = e->count;
					if (c < 1)
						c = 1;
					if (c > 16)
						c = 16;
					if (buf_byte(b, 0xE6, 0, c, 0) != 0)
						goto fail;
				} else if (e->kind == RK_PROG) {
					if (buf_byte(b, 0xEC, 0, e->count, 0) != 0)
						goto fail;
				} else if (e->kind == RK_TEXT) {
					if (buf_text(b, e->text) != 0)
						goto fail;
				}
			}
			for (h = 0; h < nh; h++) {
				int left, seen = 0;
				if (!hold[h].on)
					continue;
				left = hold[h].gate_end - t0;
				if (left <= 0)
					continue;
				for (k = 0; k < nn; k++) {
					if (a[note_at[k]].pitch == hold[h].pitch)
						seen = 1;
				}
				if (seen)
					continue;
				if (left <= st || (t0 % 240) == 0) {
					/* keep it in the sounding set for this slice */
					if (nn < 16) {
						/* virtual: encode as hold index + 1000 */
						note_at[nn++] = 1000 + h;
					}
				}
			}
			if (nn == 0 && st > 0) {
				if (buf_byte(b, 0, st, 0, 0) != 0)
					goto fail;
			} else {
				for (k = 0; k < nn; k++) {
					int pitch, vel, gate, step;
					int left;
					if (note_at[k] >= 1000) {
						h = note_at[k] - 1000;
						pitch = hold[h].pitch;
						vel = hold[h].vel;
						left = hold[h].gate_end - t0;
					} else {
						pitch = a[note_at[k]].pitch;
						vel = a[note_at[k]].vel;
						left = a[note_at[k]].gate;
					}
					if (left < 1)
						continue;
					gate = left > 255 ? 255 : left;
					step = (k == nn - 1) ? st : 0;
					if (buf_byte(b, pitch, step, gate, vel) != 0)
						goto fail;
				}
			}
			(void)emitted_note;
		}
	}
	for (i = 0; i < na; i++) {
		if (a[i].tick != end_tick)
			continue;
		if (a[i].kind == RK_BAR) {
			if (buf_byte(b, 0xFD, 0, 0, 0) != 0)
				goto fail;
		} else if (a[i].kind == RK_CH) {
			int c = a[i].count;
			if (c < 1)
				c = 1;
			if (c > 16)
				c = 16;
			if (buf_byte(b, 0xE6, 0, c, 0) != 0)
				goto fail;
		} else if (a[i].kind == RK_LE) {
			int c = a[i].count;
			if (c < 1)
				c = 2;
			if (c > 255)
				c = 255;
			if (buf_byte(b, 0xF8, 0, 0, 0) != 0)
				goto fail;
			b->p[b->n - 3] = (uint8_t)c;
		} else if (a[i].kind == RK_LS) {
			if (buf_byte(b, 0xF9, 0, 0, 0) != 0)
				goto fail;
		}
	}
	if (buf_byte(b, 0xFE, 0, 0, 0) != 0)
		goto fail;
	free(sp);
	return 0;
fail:
	free(sp);
	return -1;
}

/* The loop-end count is the second byte. buf_byte wrote step into it
 * because the call used (0xF8, 0, 0, 0) then patched n-3, which is the
 * step byte. RCP stores the count in byte +1. That patch is correct:
 * n-4 is opcode, n-3 is step/count. Good. */

static int pc_of_midi(int m)
{
	m %= 12;
	if (m < 0)
		m += 12;
	return m;
}

static int parse_chord_pcs(const char *text, int *pcs, int *npc)
{
	char buf[48];
	char *p;
	int i, let, acc, root, q;
	int intervals[4], ni = 0;
	int bass = -1;

	snprintf(buf, sizeof buf, "%s", text);
	for (i = 0; buf[i]; i++) {
		if (buf[i] == ' ') {
			memmove(&buf[i], &buf[i + 1], strlen(&buf[i + 1]) + 1);
			i--;
		}
	}
	p = buf;
	let = letter_index((unsigned char)*p);
	if (let < 0)
		return 0;
	p++;
	acc = 0;
	if (*p == '#') {
		acc = 1;
		p++;
	} else if (*p == 'b') {
		acc = -1;
		p++;
	}
	root = norm_pc(k_pc[let] + acc);
	/* Quality. Longer words first. */
	if (strncmp(p, "maj7", 4) == 0 || strncmp(p, "major7", 6) == 0 ||
	    strncmp(p, "ma7", 3) == 0 || strncmp(p, "M7", 2) == 0) {
		q = 4;
		if (strncmp(p, "major7", 6) == 0)
			p += 6;
		else if (strncmp(p, "maj7", 4) == 0)
			p += 4;
		else if (strncmp(p, "ma7", 3) == 0)
			p += 3;
		else
			p += 2;
	} else if (strncmp(p, "min7", 4) == 0 || strncmp(p, "minor7", 6) == 0 ||
		   strncmp(p, "m7", 2) == 0 || strncmp(p, "-7", 2) == 0) {
		q = 3;
		if (strncmp(p, "minor7", 6) == 0)
			p += 6;
		else if (strncmp(p, "min7", 4) == 0)
			p += 4;
		else
			p += 2;
	} else if (strncmp(p, "maj", 3) == 0 || strncmp(p, "major", 5) == 0) {
		q = 1;
		p += (strncmp(p, "major", 5) == 0) ? 5 : 3;
	} else if (strncmp(p, "min", 3) == 0 || strncmp(p, "minor", 5) == 0) {
		q = 2;
		p += (strncmp(p, "minor", 5) == 0) ? 5 : 3;
	} else if (*p == 'm' || *p == '-') {
		q = 2;
		p++;
	} else if (*p == 'M' && p[1] != '7') {
		q = 1;
		p++;
	} else if (*p == '7') {
		q = 5;
		p++;
	} else if (*p == 0 || *p == '/') {
		q = 1;
	} else {
		return 0;
	}
	if (strstr(p, "sus") || strstr(p, "dim") || strstr(p, "aug") ||
	    strstr(p, "add") || strchr(p, '6') || strchr(p, '9'))
		return 0;
	if (*p == '/') {
		int bl, ba = 0;
		p++;
		bl = letter_index((unsigned char)*p);
		if (bl < 0)
			return 0;
		p++;
		if (*p == '#') {
			ba = 1;
			p++;
		} else if (*p == 'b') {
			ba = -1;
			p++;
		}
		bass = norm_pc(k_pc[bl] + ba);
	}
	if (*p != 0)
		return 0;
	intervals[ni++] = 0;
	if (q == 2 || q == 3)
		intervals[ni++] = 3;
	else
		intervals[ni++] = 4;
	intervals[ni++] = 7;
	if (q == 3)
		intervals[ni++] = 10;
	else if (q == 4)
		intervals[ni++] = 11;
	else if (q == 5)
		intervals[ni++] = 10;
	*npc = 0;
	if (bass >= 0) {
		pcs[(*npc)++] = bass;
	}
	for (i = 0; i < ni && *npc < 6; i++) {
		int pc = norm_pc(root + intervals[i]);
		int k, dup = 0;
		for (k = 0; k < *npc; k++) {
			if (pcs[k] == pc)
				dup = 1;
		}
		if (!dup)
			pcs[(*npc)++] = pc;
	}
	return 1;
}

static void voice_closed(const int *pcs, int npc, int *midi, int *nmidi)
{
	int i, prev = 48;

	*nmidi = 0;
	for (i = 0; i < npc; i++) {
		int m = 48 + pcs[i];
		while (m < prev)
			m += 12;
		while (m > 71)
			m -= 12;
		if (m < 48)
			m += 12;
		if (m < prev && m + 12 <= 71)
			m += 12;
		if (*nmidi < 6)
			midi[(*nmidi)++] = m;
		prev = m;
	}
}

static int same_pcs(const int *midi, int nm, const int *pcs, int np)
{
	int i, k, seen[12];

	if (nm <= 1 || np <= 1)
		return 0;
	memset(seen, 0, sizeof seen);
	for (i = 0; i < nm; i++)
		seen[pc_of_midi(midi[i])] = 1;
	for (i = 0; i < np; i++) {
		if (!seen[pcs[i]])
			return 0;
	}
	for (k = 0; k < 12; k++) {
		int in = 0;
		if (!seen[k])
			continue;
		for (i = 0; i < np; i++) {
			if (pcs[i] == k)
				in = 1;
		}
		if (!in)
			return 0;
	}
	return 1;
}

static int build_accomp(Parse *ps, Atom **out, int *nout, int *end_tick)
{
	typedef struct {
		int tick;
		char text[48];
		int pcs[6];
		int np;
		int midi_n;
		int midi[6];
		int cancel;
		int voice;
	} Sym;
	Sym *sy = NULL;
	int ns = 0, cs = 0;
	int i, j, end = 0;
	Atom *a = NULL;
	int na = 0, ca = 0;

	for (i = 0; i < ps->nv; i++) {
		if (ps->v[i].end_tick > end)
			end = ps->v[i].end_tick;
		for (j = 0; j < ps->v[i].n; j++) {
			Sym s;
			int k, dupe = 0;
			if (ps->v[i].ev[j].kind != EK_SYM)
				continue;
			memset(&s, 0, sizeof s);
			s.tick = ps->v[i].ev[j].tick;
			s.voice = i;
			snprintf(s.text, sizeof s.text, "%s", ps->v[i].ev[j].text);
			if (j + 1 < ps->v[i].n && ps->v[i].ev[j + 1].kind == EK_NOTE &&
			    ps->v[i].ev[j + 1].tick == s.tick) {
				s.midi_n = ps->v[i].ev[j + 1].np;
				for (k = 0; k < s.midi_n; k++)
					s.midi[k] = ps->v[i].ev[j + 1].pit[k];
			}
			s.np = 0;
			parse_chord_pcs(s.text, s.pcs, &s.np);
			if (s.np > 0 && same_pcs(s.midi, s.midi_n, s.pcs, s.np))
				s.cancel = 1;
			for (k = 0; k < ns; k++) {
				if (sy[k].tick == s.tick) {
					dupe = 1;
					break;
				}
			}
			if (dupe)
				continue;
			{
				Sym *p = grow(sy, &cs, ns + 1, (int)sizeof(Sym));
				if (p == NULL)
					goto fail;
				sy = p;
				sy[ns++] = s;
			}
		}
	}
	if (ns == 0) {
		*out = NULL;
		*nout = 0;
		*end_tick = 0;
		free(sy);
		return 0;
	}
	/* Native loops: copy loop markers from the first voice that has them
	 * when every looping voice agrees. resolve_repeats already expanded
	 * disagreements, so leftover markers are the native ones. */
	for (i = 0; i < ps->nv; i++) {
		for (j = 0; j < ps->v[i].n; j++) {
			Atom e;
			if (ps->v[i].ev[j].kind != EK_LS &&
			    ps->v[i].ev[j].kind != EK_LE &&
			    ps->v[i].ev[j].kind != EK_BAR)
				continue;
			memset(&e, 0, sizeof e);
			e.tick = ps->v[i].ev[j].tick;
			if (ps->v[i].ev[j].kind == EK_LS) {
				e.kind = RK_LS;
				e.rank = RK_LS;
			} else if (ps->v[i].ev[j].kind == EK_LE) {
				e.kind = RK_LE;
				e.rank = RK_LE;
				e.count = 2;
			} else {
				e.kind = RK_BAR;
				e.rank = RK_BAR;
			}
			if (atom_push(&a, &na, &ca, e) != 0)
				goto fail;
		}
		break;
	}
	for (i = 0; i < ns; i++) {
		int dur, g, notes[6], nn = 0;
		int next = end;
		Atom e;
		memset(&e, 0, sizeof e);
		e.tick = sy[i].tick;
		e.kind = RK_TEXT;
		e.rank = RK_TEXT;
		snprintf(e.text, sizeof e.text, "%s", sy[i].text);
		if (atom_push(&a, &na, &ca, e) != 0)
			goto fail;
		if (i + 1 < ns && sy[i + 1].tick > sy[i].tick)
			next = sy[i + 1].tick;
		dur = next - sy[i].tick;
		if (dur < 1 || sy[i].cancel || sy[i].np <= 0)
			continue;
		voice_closed(sy[i].pcs, sy[i].np, notes, &nn);
		g = gate_pct(dur, 90);
		if (g < 1)
			continue;
		for (j = 0; j < nn; j++) {
			memset(&e, 0, sizeof e);
			e.tick = sy[i].tick;
			e.kind = RK_NOTE;
			e.rank = RK_NOTE;
			e.pitch = notes[j];
			e.vel = 100;
			e.gate = g;
			if (atom_push(&a, &na, &ca, e) != 0)
				goto fail;
		}
	}
	qsort(a, (size_t)na, sizeof(Atom), cmp_atom);
	*out = a;
	*nout = na;
	*end_tick = end;
	free(sy);
	return 1;
fail:
	free(sy);
	free(a);
	return -1;
}

static int song_add_track(AbcSong *song, const uint8_t *bytes, int len, int mch)
{
	uint8_t *c;

	if (song->ntracks >= ABC_MAX_TRACKS || len <= 0)
		return 0;
	c = malloc((size_t)len);
	if (c == NULL)
		return -1;
	memcpy(c, bytes, (size_t)len);
	song->trk[song->ntracks] = c;
	song->len[song->ntracks] = len;
	song->mch[song->ntracks] = mch;
	song->ntracks++;
	return 0;
}

static int channel_of(int index)
{
	int ch = index + 1;
	if (ch > 32)
		return 0;
	return ch;
}

int abc_import_text(const char *text, int text_len, AbcSong *song)
{
	Parse ps;
	char *buf = NULL;
	const char *p;
	int at = 1, i;
	int sig[7];

	if (song == NULL)
		return -1;
	song_clear(song);
	if (text == NULL || text_len < 0)
		return -1;
	buf = malloc((size_t)text_len + 2);
	if (buf == NULL)
		return -1;
	memcpy(buf, text, (size_t)text_len);
	buf[text_len] = 0;
	if ((unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB &&
	    (unsigned char)buf[2] == 0xBF)
		memmove(buf, buf + 3, strlen(buf + 3) + 1);

	memset(&ps, 0, sizeof ps);
	ps.tempo = 120;
	ps.beat1 = 4;
	ps.beat2 = 4;
	ps.unit_n = 1;
	ps.unit_d = 8;
	sig_from_n(0, sig);
	for (i = 0; i < 7; i++)
		ps.sig[i] = sig[i];
	ps.bkey = 0;

	p = buf;
	while (*p) {
		if (!parse_body_char(&ps, &p, &at))
			break;
		if (ps.err || ps.xcount >= 2)
			break;
	}
	for (i = 0; i < ps.nv; i++) {
		if (commit_hold(&ps.v[i]) != 0)
			ps.err = 1;
		if (ps.v[i].has_in) {
			Ev e;
			memset(&e, 0, sizeof e);
			e.kind = EK_SYM;
			snprintf(e.text, sizeof e.text, "%s", ps.v[i].incoming);
			if (push_ev(&ps.v[i], e) != 0)
				ps.err = 1;
			ps.v[i].has_in = 0;
		}
	}
	if (!ps.err && resolve_repeats(&ps) != 0)
		ps.err = 1;

	{
		int nvoice = 0;
		int inline_sym = 0;
		for (i = 0; i < ps.nv; i++) {
			int has = 0, k;
			for (k = 0; k < ps.v[i].n; k++) {
				if (ps.v[i].ev[k].kind == EK_NOTE ||
				    ps.v[i].ev[k].kind == EK_REST ||
				    ps.v[i].ev[k].kind == EK_BAR)
					has = 1;
			}
			if (has || ps.v[i].prog >= 0)
				nvoice++;
		}
		if (nvoice >= ABC_MAX_TRACKS)
			inline_sym = 1;

	if (!ps.err) {
		snprintf(song->title, sizeof song->title, "%s", ps.title);
		song->tempo = ps.tempo;
		song->beat1 = ps.beat1;
		song->beat2 = ps.beat2;
		song->key = ps.bkey;
		song->timebase = TB;
		for (i = 0; i < ps.nv && !ps.err; i++) {
			Atom *atoms = NULL;
			int na = 0, end = 0;
			Buf b;
			int has = 0, k;
			memset(&b, 0, sizeof b);
			for (k = 0; k < ps.v[i].n; k++) {
				if (ps.v[i].ev[k].kind == EK_NOTE ||
				    ps.v[i].ev[k].kind == EK_REST ||
				    ps.v[i].ev[k].kind == EK_BAR)
					has = 1;
			}
			if (!has && ps.v[i].prog < 0)
				continue;
			if (voice_atoms(&ps.v[i], &atoms, &na, &end, inline_sym) != 0 ||
			    emit_atoms(atoms, na, end, &b) != 0) {
				ps.err = 1;
				free(atoms);
				free(b.p);
				break;
			}
			{
				int before = song->ntracks;
				if (song_add_track(song, b.p, b.n,
						   channel_of(song->ntracks)) != 0)
					ps.err = 1;
				else if (song->ntracks > before)
					copy_memo(song->memo[song->ntracks - 1],
						  ps.v[i].memo);
			}
			free(atoms);
			free(b.p);
		}
	}
	if (!ps.err) {
		Atom *atoms = NULL;
		int na = 0, end = 0, rc;
		Buf b;
		memset(&b, 0, sizeof b);
		rc = build_accomp(&ps, &atoms, &na, &end);
		if (rc < 0)
			ps.err = 1;
		else if (rc > 0 && !inline_sym) {
			int before = song->ntracks;
			if (emit_atoms(atoms, na, end, &b) != 0 ||
			    song_add_track(song, b.p, b.n,
					   channel_of(song->ntracks)) != 0) {
				ps.err = 1;
			} else if (song->ntracks > before) {
				copy_memo(song->memo[song->ntracks - 1], "Chord");
			}
		}
		free(atoms);
		free(b.p);
	}
	}

	for (i = 0; i < ps.nv; i++)
		free(ps.v[i].ev);
	free(buf);
	if (ps.err) {
		abc_song_free(song);
		song_clear(song);
		return -1;
	}
	return 0;
}
