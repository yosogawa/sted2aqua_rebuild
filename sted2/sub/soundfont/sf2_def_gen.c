/*
 * sf2_def_gen.c — SF2 → DEF skeleton generator (C).
 * #rhy_as comes from the first bank 128 kit that has per-key names.
 */

#include "sf2_def_gen.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h> /* getenv */

#include "../../version.h" /* VERSION_ID — same rebuild id as About / banner */

#define TSF_IMPLEMENTATION
#include "tsf.h"

enum {
	kToneNameWidth = 12,
	kProgCount = 128,
	kMelodicBankMax = 128, /* 0..127 melodic; 128 = percussion */
	kFirstTonePart = 16,
	/* sted2def.doc: #module= display name up to 22; #id= uses same budget */
	kIdentNameMax = 22
};

typedef char Sf2ToneName[kToneNameWidth + 1];
typedef Sf2ToneName Sf2BankPresets[kProgCount];

static void
sf2_pad_name(char *dst, int width, const char *src)
{
	int i = 0;
	if (src == NULL)
		src = "";
	while (src[i] != 0 && i < width) {
		unsigned char c = (unsigned char)src[i];
		dst[i] = (c >= 0x20 && c < 0x7f) ? (char)c : '?';
		i++;
	}
	while (i < width)
		dst[i++] = ' ';
	dst[width] = 0;
}

static void
sf2_sanitize_basename(char *dst, size_t n, const char *sf2_path)
{
	const char *base, *slash, *dot;
	size_t i, j = 0;

	if (n == 0)
		return;
	slash = strrchr(sf2_path, '/');
	base = slash ? slash + 1 : sf2_path;
	dot = strrchr(base, '.');
	for (i = 0; base[i] != 0 && (dot == NULL || &base[i] < dot) && j + 1 < n; i++) {
		unsigned char c = (unsigned char)base[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		    (c >= '0' && c <= '9') || c == '_' || c == '-')
			dst[j++] = (char)c;
		else if (c == ' ' || c == '.')
			dst[j++] = '_';
	}
	if (j == 0) {
		strncpy(dst, "sf2bank", n - 1);
		dst[n - 1] = 0;
		return;
	}
	dst[j] = 0;
}

static void
sf2_module_label(char *dst, size_t n, const char *sf2_path)
{
	const char *base, *slash, *dot;
	size_t len;

	if (n == 0)
		return;
	slash = strrchr(sf2_path, '/');
	base = slash ? slash + 1 : sf2_path;
	dot = strrchr(base, '.');
	len = dot ? (size_t)(dot - base) : strlen(base);
	if (len > (size_t)kIdentNameMax)
		len = (size_t)kIdentNameMax;
	if (len >= n)
		len = n - 1;
	memcpy(dst, base, len);
	dst[len] = 0;
}

/* #id= token from SF2 basename (safe chars), capped at kIdentNameMax. */
static void
sf2_id_label(char *dst, size_t n, const char *sf2_path)
{
	char base[256];
	size_t len;

	if (dst == NULL || n == 0)
		return;
	sf2_sanitize_basename(base, sizeof base, sf2_path);
	len = strlen(base);
	if (len > (size_t)kIdentNameMax)
		len = (size_t)kIdentNameMax;
	if (len >= n)
		len = n - 1;
	if (len == 0) {
		strncpy(dst, "SF2", n - 1);
		dst[n - 1] = 0;
		return;
	}
	memcpy(dst, base, len);
	dst[len] = 0;
}

enum {
	/* PART ASSIGN field matches classic heads like "Roland GS format" (16). */
	kPartHeadWidth = 16
};

/*
 * PART ASSIGN title from #id=.
 * Drums: width is fixed; always place "_Drums" at the end, trim id to fit.
 */
static void
sf2_part_head(char *dst, size_t n, const char *id, int drums)
{
	static const char kDrumsSuffix[] = "_Drums";
	const char *src = (id != NULL && id[0]) ? id : "SF2";
	int width = kPartHeadWidth;
	int i, id_max;
	size_t suf_len;

	if (dst == NULL || n == 0)
		return;
	if ((int)(n - 1) < width)
		width = (int)(n - 1);
	if (!drums) {
		sf2_pad_name(dst, width, src);
		return;
	}
	suf_len = sizeof kDrumsSuffix - 1; /* 6 */
	if (width <= (int)suf_len) {
		sf2_pad_name(dst, width, kDrumsSuffix);
		return;
	}
	id_max = width - (int)suf_len;
	for (i = 0; i < id_max && src[i] != 0; i++)
		dst[i] = src[i];
	while (i < id_max)
		dst[i++] = ' ';
	memcpy(dst + id_max, kDrumsSuffix, suf_len);
	dst[width] = 0;
}

static void
sf2_part_sym(char *dst, size_t n, const char *id)
{
	const char *src = (id != NULL && id[0]) ? id : "SF2";
	size_t i, max = (n > 2) ? n - 2 : 0; /* leave room for ':' and NUL */

	if (dst == NULL || n == 0)
		return;
	for (i = 0; i < max && src[i] != 0; i++)
		dst[i] = src[i];
	dst[i++] = ':';
	dst[i] = 0;
}

enum {
	kRhySlots = 32,
	kRhyNameWidth = 14,
	kSf2Name = 21,
	kSf2MaxPhdr = 8192,
	kSf2MaxBag = 65536,
	kSf2MaxGen = 65536,
	kSf2MaxInst = 16384,
	kSf2MaxShdr = 16384,
	kSf2MaxChunk = 8 * 1024 * 1024
};

/* SoundFont 2.04 generators used to map a drum key to a name. */
enum {
	kGenInstrument = 41,
	kGenKeyRange = 43,
	kGenVelRange = 44,
	kGenSampleID = 53
};

struct Sf2RhyHit {
	char name[kSf2Name];
	int note;
};

struct Sf2RhyMap {
	struct Sf2RhyHit hit[kRhySlots];
	int count;	/* slots written, 0..32 */
	int found;	/* keyed drums in the kit, before the 32 cap */
	int program;
};

struct Sf2Phdr {
	unsigned short preset, bank, bag;
};

struct Sf2Bag {
	unsigned short gen, mod;
};

struct Sf2Gen {
	unsigned short oper, amt;
	unsigned char lo, hi;
};

struct Sf2Inst {
	char name[kSf2Name];
	unsigned short bag;
};

struct Sf2Shdr {
	char name[kSf2Name];
};

struct Sf2Pdta {
	struct Sf2Phdr *phdr; int phdrN;
	struct Sf2Bag *pbag; int pbagN;
	struct Sf2Gen *pgen; int pgenN;
	struct Sf2Inst *inst; int instN;
	struct Sf2Bag *ibag; int ibagN;
	struct Sf2Gen *igen; int igenN;
	struct Sf2Shdr *shdr; int shdrN;
};

struct Sf2NotePick {
	int used;
	int inst;
	int keySpan;
	int velSpan;
	int covers100;
	char instName[kSf2Name];
	char sampleName[kSf2Name];
};

static unsigned short
sf2_u16(const unsigned char *p)
{
	return (unsigned short)((unsigned int)p[0] | ((unsigned int)p[1] << 8));
}

static unsigned int
sf2_u32(const unsigned char *p)
{
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
		((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static void
sf2_copy_sfname(char *dst, const unsigned char *src)
{
	int i, n = 0, b = 0;

	for (i = 0; i < 20 && src[i] != 0; i++) {
		unsigned char c = src[i];
		/* ',' splits a DEF field. '*' starts a comment in defload. */
		if (c < 0x20 || c >= 0x7f || c == ',' || c == '*')
			c = ' ';
		dst[n++] = (char)c;
	}
	dst[n] = 0;
	while (n > 0 && dst[n - 1] == ' ')
		dst[--n] = 0;
	while (dst[b] == ' ')
		b++;
	if (b > 0)
		memmove(dst, dst + b, strlen(dst + b) + 1);
}

static int
sf2_read_full(FILE *fp, void *buf, size_t n)
{
	return n == 0 || fread(buf, 1, n, fp) == n;
}

static int
sf2_skip(FILE *fp, unsigned int n)
{
	return fseeko(fp, (off_t)n, SEEK_CUR) == 0;
}

static int
sf2_clamp_range(int *lo, int *hi)
{
	if (*lo < 0)
		*lo = 0;
	if (*hi < 0)
		*hi = 0;
	if (*lo > 127)
		*lo = 127;
	if (*hi > 127)
		*hi = 127;
	return *lo <= *hi;
}

static int
sf2_pick_better(const struct Sf2NotePick *n, const struct Sf2NotePick *o)
{
	if (!o->used)
		return 1;
	if (n->keySpan != o->keySpan)
		return n->keySpan < o->keySpan;
	/* A 0-127 placeholder (often silent) loses to the band that contains
	 * a normal hit, and the narrower of those bands is the one authored
	 * for that velocity. */
	if (n->covers100 != o->covers100)
		return n->covers100;
	if (n->velSpan != o->velSpan)
		return n->velSpan < o->velSpan;
	return 0;
}

static void
sf2_pdta_free(struct Sf2Pdta *d)
{
	free(d->phdr);
	free(d->pbag);
	free(d->pgen);
	free(d->inst);
	free(d->ibag);
	free(d->igen);
	free(d->shdr);
	memset(d, 0, sizeof *d);
}

static int
sf2_load_phdr(const unsigned char *b, unsigned int n, struct Sf2Phdr **out, int *count)
{
	int i, c;

	if (n % 38 != 0)
		return -1;
	c = (int)(n / 38);
	if (c < 2 || c > kSf2MaxPhdr)
		return -1;
	*out = calloc((size_t)c, sizeof **out);
	if (*out == NULL)
		return -1;
	for (i = 0; i < c; i++) {
		const unsigned char *p = b + (size_t)i * 38;
		(*out)[i].preset = sf2_u16(p + 20);
		(*out)[i].bank = sf2_u16(p + 22);
		(*out)[i].bag = sf2_u16(p + 24);
	}
	*count = c;
	return 0;
}

static int
sf2_load_bags(const unsigned char *b, unsigned int n, struct Sf2Bag **out, int *count)
{
	int i, c;

	if (n % 4 != 0)
		return -1;
	c = (int)(n / 4);
	if (c < 2 || c > kSf2MaxBag)
		return -1;
	*out = calloc((size_t)c, sizeof **out);
	if (*out == NULL)
		return -1;
	for (i = 0; i < c; i++) {
		const unsigned char *p = b + (size_t)i * 4;
		(*out)[i].gen = sf2_u16(p);
		(*out)[i].mod = sf2_u16(p + 2);
	}
	*count = c;
	return 0;
}

static int
sf2_load_gens(const unsigned char *b, unsigned int n, struct Sf2Gen **out, int *count)
{
	int i, c;

	if (n % 4 != 0)
		return -1;
	c = (int)(n / 4);
	if (c < 1 || c > kSf2MaxGen)
		return -1;
	*out = calloc((size_t)c, sizeof **out);
	if (*out == NULL)
		return -1;
	for (i = 0; i < c; i++) {
		const unsigned char *p = b + (size_t)i * 4;
		(*out)[i].oper = sf2_u16(p);
		(*out)[i].amt = sf2_u16(p + 2);
		(*out)[i].lo = p[2];
		(*out)[i].hi = p[3];
	}
	*count = c;
	return 0;
}

static int
sf2_load_inst(const unsigned char *b, unsigned int n, struct Sf2Inst **out, int *count)
{
	int i, c;

	if (n % 22 != 0)
		return -1;
	c = (int)(n / 22);
	if (c < 2 || c > kSf2MaxInst)
		return -1;
	*out = calloc((size_t)c, sizeof **out);
	if (*out == NULL)
		return -1;
	for (i = 0; i < c; i++) {
		const unsigned char *p = b + (size_t)i * 22;
		sf2_copy_sfname((*out)[i].name, p);
		(*out)[i].bag = sf2_u16(p + 20);
	}
	*count = c;
	return 0;
}

static int
sf2_load_shdr(const unsigned char *b, unsigned int n, struct Sf2Shdr **out, int *count)
{
	int i, c;

	if (n % 46 != 0)
		return -1;
	c = (int)(n / 46);
	if (c < 2 || c > kSf2MaxShdr)
		return -1;
	*out = calloc((size_t)c, sizeof **out);
	if (*out == NULL)
		return -1;
	for (i = 0; i < c; i++)
		sf2_copy_sfname((*out)[i].name, b + (size_t)i * 46);
	*count = c;
	return 0;
}

static unsigned char *
sf2_read_chunk(FILE *fp, unsigned int sz)
{
	unsigned char *body;

	if (sz > (unsigned int)kSf2MaxChunk)
		return NULL;
	body = malloc(sz ? sz : 1);
	if (body == NULL)
		return NULL;
	if (!sf2_read_full(fp, body, sz)) {
		free(body);
		return NULL;
	}
	return body;
}

static int
sf2_parse_pdta(FILE *fp, unsigned int left, struct Sf2Pdta *d)
{
	while (left >= 8) {
		unsigned char hdr[8];
		unsigned int sz;
		int rc = 0;

		if (!sf2_read_full(fp, hdr, 8))
			return -1;
		left -= 8;
		sz = sf2_u32(hdr + 4);
		if (sz > left)
			return -1;
		if (memcmp(hdr, "phdr", 4) == 0 || memcmp(hdr, "pbag", 4) == 0 ||
		    memcmp(hdr, "pgen", 4) == 0 || memcmp(hdr, "inst", 4) == 0 ||
		    memcmp(hdr, "ibag", 4) == 0 || memcmp(hdr, "igen", 4) == 0 ||
		    memcmp(hdr, "shdr", 4) == 0) {
			unsigned char *body = sf2_read_chunk(fp, sz);
			if (body == NULL)
				return -1;
			if (memcmp(hdr, "phdr", 4) == 0) {
				free(d->phdr);
				d->phdr = NULL;
				d->phdrN = 0;
				rc = sf2_load_phdr(body, sz, &d->phdr, &d->phdrN);
			} else if (memcmp(hdr, "pbag", 4) == 0) {
				free(d->pbag);
				d->pbag = NULL;
				d->pbagN = 0;
				rc = sf2_load_bags(body, sz, &d->pbag, &d->pbagN);
			} else if (memcmp(hdr, "pgen", 4) == 0) {
				free(d->pgen);
				d->pgen = NULL;
				d->pgenN = 0;
				rc = sf2_load_gens(body, sz, &d->pgen, &d->pgenN);
			} else if (memcmp(hdr, "inst", 4) == 0) {
				free(d->inst);
				d->inst = NULL;
				d->instN = 0;
				rc = sf2_load_inst(body, sz, &d->inst, &d->instN);
			} else if (memcmp(hdr, "ibag", 4) == 0) {
				free(d->ibag);
				d->ibag = NULL;
				d->ibagN = 0;
				rc = sf2_load_bags(body, sz, &d->ibag, &d->ibagN);
			} else if (memcmp(hdr, "igen", 4) == 0) {
				free(d->igen);
				d->igen = NULL;
				d->igenN = 0;
				rc = sf2_load_gens(body, sz, &d->igen, &d->igenN);
			} else {
				free(d->shdr);
				d->shdr = NULL;
				d->shdrN = 0;
				rc = sf2_load_shdr(body, sz, &d->shdr, &d->shdrN);
			}
			free(body);
		} else if (!sf2_skip(fp, sz))
			return -1;
		if (rc != 0)
			return -1;
		left -= sz;
		if (sz & 1) {
			if (left == 0 || !sf2_skip(fp, 1))
				return -1;
			left--;
		}
	}
	if (left != 0 && !sf2_skip(fp, left))
		return -1;
	return 0;
}

static void
sf2_apply_inst(const struct Sf2Pdta *d, int instIndex,
	       int plokey, int phikey, int plovel, int phivel,
	       struct Sf2NotePick *picks)
{
	const struct Sf2Inst *in;
	int ib0, ib1, j;
	int iglo = 0, ighi = 127, ivlo = 0, ivhi = 127;

	if (instIndex < 0 || instIndex + 1 >= d->instN)
		return;
	in = &d->inst[instIndex];
	ib0 = in->bag;
	ib1 = d->inst[instIndex + 1].bag;
	if (ib0 < 0 || ib1 < ib0 || ib1 >= d->ibagN)
		return;
	for (j = ib0; j < ib1; j++) {
		int g0, g1, g, samp, zlo, zhi, zvlo, zvhi;

		g0 = d->ibag[j].gen;
		g1 = d->ibag[j + 1].gen;
		if (g0 < 0 || g1 < g0 || g1 > d->igenN)
			continue;
		zlo = iglo;
		zhi = ighi;
		zvlo = ivlo;
		zvhi = ivhi;
		samp = -1;
		for (g = g0; g < g1; g++) {
			unsigned short op = d->igen[g].oper;
			if (op == kGenKeyRange) {
				zlo = d->igen[g].lo;
				zhi = d->igen[g].hi;
			} else if (op == kGenVelRange) {
				zvlo = d->igen[g].lo;
				zvhi = d->igen[g].hi;
			} else if (op == kGenSampleID)
				samp = d->igen[g].amt;
		}
		if (!sf2_clamp_range(&zlo, &zhi) || !sf2_clamp_range(&zvlo, &zvhi))
			continue;
		if (j == ib0 && samp < 0) {
			iglo = zlo;
			ighi = zhi;
			ivlo = zvlo;
			ivhi = zvhi;
			continue;
		}
		if (samp < 0 || samp + 1 >= d->shdrN)
			continue;
		if (zhi < plokey || zlo > phikey || zvhi < plovel || zvlo > phivel)
			continue;
		if (zlo < plokey)
			zlo = plokey;
		if (zhi > phikey)
			zhi = phikey;
		if (zvlo < plovel)
			zvlo = plovel;
		if (zvhi > phivel)
			zvhi = phivel;
		if (zlo <= zhi && zvlo <= zvhi) {
			struct Sf2NotePick hit;
			int note;

			memset(&hit, 0, sizeof hit);
			hit.used = 1;
			hit.inst = instIndex;
			hit.keySpan = zhi - zlo;
			hit.velSpan = zvhi - zvlo;
			hit.covers100 = (zvlo <= 100 && zvhi >= 100);
			memcpy(hit.instName, in->name, sizeof hit.instName);
			memcpy(hit.sampleName, d->shdr[samp].name, sizeof hit.sampleName);
			for (note = zlo; note <= zhi; note++) {
				if (sf2_pick_better(&hit, &picks[note]))
					picks[note] = hit;
			}
		}
	}
}

static void
sf2_add_rhy(struct Sf2RhyMap *map, char names[][kSf2Name], const int *present,
	    int *used, int note)
{
	if (map->count >= kRhySlots || note < 0 || note > 127)
		return;
	if (!present[note] || used[note])
		return;
	snprintf(map->hit[map->count].name, sizeof map->hit[map->count].name,
		 "%s", names[note]);
	map->hit[map->count].note = note;
	used[note] = 1;
	map->count++;
}

static void
sf2_pick_rhy_slots(char names[][kSf2Name], const int *present, int found,
		   struct Sf2RhyMap *map)
{
	/* Same 30 keys as gm.def's rhythm assign, in that screen order. */
	static const unsigned char kGmNotes[] = {
		39, 62, 63, 64, 49, 51, 44, 46, 42, 48, 45, 41,
		37, 40, 38, 36, 65, 66, 56, 60, 61, 67, 68, 54,
		75, 70, 71, 72, 69, 73
	};
	int used[128];
	int note, i;

	memset(used, 0, sizeof used);
	map->count = 0;
	if (found <= kRhySlots) {
		for (note = 0; note < 128; note++)
			sf2_add_rhy(map, names, present, used, note);
		return;
	}
	for (i = 0; i < (int)(sizeof kGmNotes); i++)
		sf2_add_rhy(map, names, present, used, kGmNotes[i]);
	for (note = 35; note <= 81; note++)
		sf2_add_rhy(map, names, present, used, note);
	for (note = 0; note < 128; note++)
		sf2_add_rhy(map, names, present, used, note);
}

static int
sf2_rhy_from_preset(const struct Sf2Pdta *d, int presetIndex, struct Sf2RhyMap *map)
{
	struct Sf2NotePick picks[128];
	char names[128][kSf2Name];
	int present[128];
	int b0, b1, bi;
	int pglo = 0, pghi = 127, pvlo = 0, pvhi = 127;
	int note, found;

	if (presetIndex < 0 || presetIndex + 1 >= d->phdrN)
		return 0;
	b0 = d->phdr[presetIndex].bag;
	b1 = d->phdr[presetIndex + 1].bag;
	if (b0 < 0 || b1 < b0 || b1 >= d->pbagN)
		return 0;
	memset(picks, 0, sizeof picks);
	for (bi = b0; bi < b1; bi++) {
		int g0, g1, g, ninst, lokey, hikey, lovel, hivel;
		int insts[32];

		g0 = d->pbag[bi].gen;
		g1 = d->pbag[bi + 1].gen;
		if (g0 < 0 || g1 < g0 || g1 > d->pgenN)
			continue;
		lokey = pglo;
		hikey = pghi;
		lovel = pvlo;
		hivel = pvhi;
		ninst = 0;
		for (g = g0; g < g1; g++) {
			unsigned short op = d->pgen[g].oper;
			if (op == kGenKeyRange) {
				lokey = d->pgen[g].lo;
				hikey = d->pgen[g].hi;
			} else if (op == kGenVelRange) {
				lovel = d->pgen[g].lo;
				hivel = d->pgen[g].hi;
			} else if (op == kGenInstrument && ninst < 32)
				insts[ninst++] = d->pgen[g].amt;
		}
		if (!sf2_clamp_range(&lokey, &hikey) || !sf2_clamp_range(&lovel, &hivel))
			continue;
		if (bi == b0 && ninst == 0) {
			pglo = lokey;
			pghi = hikey;
			pvlo = lovel;
			pvhi = hivel;
			continue;
		}
		for (g = 0; g < ninst; g++)
			sf2_apply_inst(d, insts[g], lokey, hikey, lovel, hivel, picks);
	}

	memset(names, 0, sizeof names);
	memset(present, 0, sizeof present);
	found = 0;
	for (note = 0; note < 128; note++) {
		int shared = 0, k;
		const char *nm = NULL;

		if (!picks[note].used)
			continue;
		for (k = 0; k < 128; k++) {
			if (picks[k].used && picks[k].inst == picks[note].inst)
				shared++;
		}
		if (shared <= 1 && picks[note].instName[0] != 0)
			nm = picks[note].instName;
		else if (picks[note].sampleName[0] != 0)
			nm = picks[note].sampleName;
		else if (picks[note].instName[0] != 0)
			nm = picks[note].instName;
		if (nm == NULL || nm[0] == 0)
			continue;
		snprintf(names[note], sizeof names[note], "%s", nm);
		present[note] = 1;
		found++;
	}
	if (found == 0)
		return 0;
	sf2_pick_rhy_slots(names, present, found, map);
	map->program = d->phdr[presetIndex].preset;
	map->found = found;
	return map->count > 0;
}

static void
sf2_rhy_from_pdta(const struct Sf2Pdta *d, struct Sf2RhyMap *map)
{
	int idx[128];
	int n = 0, i, a, b;

	if (d->phdrN < 2 || d->pbagN < 2 || d->pgenN < 1 || d->instN < 2 ||
	    d->ibagN < 2 || d->igenN < 1 || d->shdrN < 2)
		return;
	for (i = 0; i < d->phdrN - 1 && n < (int)(sizeof idx / sizeof idx[0]); i++) {
		if (d->phdr[i].bank == 128)
			idx[n++] = i;
	}
	for (a = 0; a < n; a++) {
		for (b = a + 1; b < n; b++) {
			unsigned short pa = d->phdr[idx[a]].preset;
			unsigned short pb = d->phdr[idx[b]].preset;
			if (pb < pa || (pb == pa && idx[b] < idx[a])) {
				int tmp = idx[a];
				idx[a] = idx[b];
				idx[b] = tmp;
			}
		}
	}
	for (a = 0; a < n; a++) {
		if (sf2_rhy_from_preset(d, idx[a], map))
			return;
	}
}

/* Walk RIFF until pdta. Failure leaves map.count at 0; DEF generation continues. */
static void
sf2_extract_rhythm(const char *path, struct Sf2RhyMap *map)
{
	FILE *fp;
	unsigned char hdr[12];
	int saw = 0;

	memset(map, 0, sizeof *map);
	fp = fopen(path, "rb");
	if (fp == NULL)
		return;
	if (!sf2_read_full(fp, hdr, 12) || memcmp(hdr, "RIFF", 4) != 0 ||
	    memcmp(hdr + 8, "sfbk", 4) != 0) {
		fclose(fp);
		return;
	}
	for (;;) {
		unsigned char ch[8];
		unsigned int sz;

		if (!sf2_read_full(fp, ch, 8))
			break;
		sz = sf2_u32(ch + 4);
		if (memcmp(ch, "LIST", 4) == 0 && sz >= 4) {
			char typ[4];

			if (!sf2_read_full(fp, typ, 4))
				break;
			if (!saw && memcmp(typ, "pdta", 4) == 0) {
				struct Sf2Pdta pdta;

				memset(&pdta, 0, sizeof pdta);
				saw = 1;
				if (sf2_parse_pdta(fp, sz - 4, &pdta) == 0)
					sf2_rhy_from_pdta(&pdta, map);
				sf2_pdta_free(&pdta);
			} else if (!sf2_skip(fp, sz - 4))
				break;
			if ((sz & 1) && !sf2_skip(fp, 1))
				break;
		} else {
			if (!sf2_skip(fp, sz))
				break;
			if ((sz & 1) && !sf2_skip(fp, 1))
				break;
		}
	}
	fclose(fp);
}

static void
sf2_write_rhy_block(FILE *fp, const struct Sf2RhyMap *rhy)
{
	int i, n;

	fprintf(fp, "#rhy_as=10,0,-1,,127\r\n");
	if (rhy == NULL || rhy->count <= 0) {
		fprintf(fp,
			"*--------------------------------------------------------------------------------------\r\n"
			"              ,  0, 1,              ,  0, 1,              ,  0, 1,              ,  0, 1\r\n"
			"              ,  0, 1,              ,  0, 1,              ,  0, 1,              ,  0, 1\r\n"
			"              ,  0, 1,              ,  0, 1,              ,  0, 1,              ,  0, 1\r\n"
			"              ,  0, 1,              ,  0, 1,              ,  0, 1,              ,  0, 1\r\n"
			"              ,  0, 1,              ,  0, 1,              ,  0, 1,              ,  0, 1\r\n"
			"              ,  0, 1,              ,  0, 1,              ,  0, 1,              ,  0, 1\r\n"
			"              ,  0, 1,              ,  0, 1,              ,  0, 1,              ,  0, 1\r\n"
			"              ,  0, 1,              ,  0, 1,              ,  0, 1,              ,  0, 1\r\n"
			"*--------------------------------------------------------------------------------------\r\n"
			"#defoff\r\n");
		return;
	}
	fprintf(fp,
		"* Rhythm assign from SF2 bank 128 program %d (%d keys). Gate is 1.\r\n"
		"*--------------------------------------------------------------------------------------\r\n",
		rhy->program, rhy->found);
	n = rhy->count;
	if (n > kRhySlots)
		n = kRhySlots;
	for (i = 0; i < kRhySlots; i++) {
		char name[kRhyNameWidth + 1];
		int note = 0;

		if (i < n) {
			sf2_pad_name(name, kRhyNameWidth, rhy->hit[i].name);
			note = rhy->hit[i].note;
		} else
			sf2_pad_name(name, kRhyNameWidth, "");
		fprintf(fp, "%s,%3d,%2d", name, note, 1);
		if ((i & 3) == 3)
			fprintf(fp, "\r\n");
		else
			fprintf(fp, ",");
	}
	fprintf(fp,
		"*--------------------------------------------------------------------------------------\r\n"
		"#defoff\r\n");
}

static int
sf2_write_def(const char *outPath, const char *moduleLabel, const char *idLabel,
	      Sf2BankPresets *presets, const int *bankUsed,
	      const struct Sf2RhyMap *rhy)
{
	FILE *fp;
	int bank, prog, part;
	int bankToPart[kMelodicBankMax];
	int nextPart = kFirstTonePart;
	int hasPerc = bankUsed[kMelodicBankMax];
	char name12[kToneNameWidth + 1];
	char partHead[kPartHeadWidth + 1], partSym[8], drumHead[kPartHeadWidth + 1];
	const char *id = (idLabel != NULL && idLabel[0]) ? idLabel : "SF2";

	fp = fopen(outPath, "wb");
	if (fp == NULL)
		return -1;

	sf2_part_head(partHead, sizeof partHead, id, 0);
	sf2_part_head(drumHead, sizeof drumHead, id, 1);
	sf2_part_sym(partSym, sizeof partSym, id);

	for (bank = 0; bank < kMelodicBankMax; bank++)
		bankToPart[bank] = 0;
	for (bank = 0; bank < kMelodicBankMax; bank++) {
		if (!bankUsed[bank])
			continue;
		bankToPart[bank] = nextPart++;
		if (nextPart >= 400)
			break;
	}

	fprintf(fp,
		"*--------------------------------------------------------------------------------------\r\n"
		"* Auto-generated from SoundFont2 by STed2.07(%s).\r\n"
		"* Melodic banks map via #gs_pbank; percussion bank 128 is listed as kits.\r\n"
		"* #rhy_as is filled from the first bank 128 kit when per-key names exist.\r\n"
		"*--------------------------------------------------------------------------------------\r\n"
		"\r\n"
		"#module= A: %.*s\r\n"
		"\r\n"
		"#id=%.*s\r\n"
		"\r\n"
		"#lsp_wait=1\r\n"
		"\r\n"
		"#rec_met=10,  0,  37, 2,106,  56, 2,120\r\n"
		"\r\n",
		VERSION_ID,
		kIdentNameMax, moduleLabel != NULL && moduleLabel[0] ? moduleLabel : id,
		kIdentNameMax, id);

	/* CH10 → part 8 so PART ASSIGN can show a distinct "*_Drums" bank. */
	fprintf(fp,
		"#group_a=7,7,7,7,7,7,7,7,7,8,7,7,7,7,7,7\r\n"
		"#group_b=7,7,7,7,7,7,7,7,7,8,7,7,7,7,7,7\r\n"
		"\r\n"
		"#gs_mode=%d\r\n"
		"\r\n",
		hasPerc ? 2 : 0);

	fprintf(fp, "#gs_pbank\r\n");
	for (bank = 0; bank < kMelodicBankMax; bank++) {
		fprintf(fp, "%d", bankToPart[bank]);
		if ((bank & 7) == 7)
			fprintf(fp, "\r\n");
		else
			fprintf(fp, ",");
	}
	fprintf(fp, "#defoff\r\n\r\n");

	if (hasPerc) {
		fprintf(fp, "#gs_rbank\r\n");
		for (prog = 0; prog < kProgCount; prog++) {
			int slot = (presets[kMelodicBankMax][prog][0] != 0) ? 8 : 0;
			fprintf(fp, "%d", slot);
			if ((prog & 7) == 7)
				fprintf(fp, "\r\n");
			else
				fprintf(fp, ",");
		}
		fprintf(fp, "#defoff\r\n\r\n");
	}

	sf2_write_rhy_block(fp, rhy);
	fprintf(fp,
		"\r\n"
		"*======================================================================================\r\n"
		"* Tone names (12 chars). Generated from SF2 presets.\r\n"
		"\r\n"
		"#part=7, %s,%s\r\n"
		"*--------------------------------------------------------------------------------------\r\n",
		partHead, partSym);

	if (bankUsed[0]) {
		for (prog = 0; prog < kProgCount; prog++) {
			sf2_pad_name(name12, kToneNameWidth, presets[0][prog]);
			fprintf(fp, "%s", name12);
			if ((prog & 3) == 3)
				fprintf(fp, "\r\n");
			else
				fprintf(fp, ",");
		}
	}
	fprintf(fp, "*--------------------------------------------------------------------------------------\r\n#defoff\r\n\r\n");

	for (bank = 0; bank < kMelodicBankMax; bank++) {
		if (!bankUsed[bank])
			continue;
		part = bankToPart[bank];
		fprintf(fp,
			"#part=%d, Bank%-3d      ,B%d:\r\n"
			"*--------------------------------------------------------------------------------------\r\n",
			part, bank, bank);
		for (prog = 0; prog < kProgCount; prog++) {
			sf2_pad_name(name12, kToneNameWidth, presets[bank][prog]);
			fprintf(fp, "%s", name12);
			if ((prog & 3) == 3)
				fprintf(fp, "\r\n");
			else
				fprintf(fp, ",");
		}
		fprintf(fp, "*--------------------------------------------------------------------------------------\r\n#defoff\r\n\r\n");
	}

	/*
	 * Part 8 = drum kit program list (prog 0..127), same layout as melody
	 * parts. Comment-only lines are ignored by defload — emit real names.
	 * Per-kit key maps remain TODO (sc55 uses separate #part=32+ / #gs_rbank).
	 */
	fprintf(fp,
		"#part=8, %s,%s\r\n"
		"*--------------------------------------------------------------------------------------\r\n",
		drumHead, partSym);
	for (prog = 0; prog < kProgCount; prog++) {
		if (hasPerc && presets[kMelodicBankMax][prog][0] != 0)
			sf2_pad_name(name12, kToneNameWidth, presets[kMelodicBankMax][prog]);
		else
			sf2_pad_name(name12, kToneNameWidth, "");
		fprintf(fp, "%s", name12);
		if ((prog & 3) == 3)
			fprintf(fp, "\r\n");
		else
			fprintf(fp, ",");
	}
	fprintf(fp, "*--------------------------------------------------------------------------------------\r\n#defoff\r\n");

	fclose(fp);
	return 0;
}

static void
sf2_set_err(char *errbuf, size_t errbuf_sz, const char *msg)
{
	if (errbuf == NULL || errbuf_sz == 0)
		return;
	strncpy(errbuf, msg != NULL ? msg : "error", errbuf_sz - 1);
	errbuf[errbuf_sz - 1] = 0;
}

void
sf2_def_default_banks_dir(char *buf, size_t n)
{
	const char *home;
	if (buf == NULL || n == 0)
		return;
	home = getenv("HOME");
	if (home == NULL || home[0] == 0) {
		buf[0] = 0;
		return;
	}
	snprintf(buf, n, "%s/Library/Audio/Sounds/Banks", home);
}

void
sf2_def_default_out_dir(char *buf, size_t n)
{
	const char *home;
	if (buf == NULL || n == 0)
		return;
	home = getenv("HOME");
	if (home == NULL || home[0] == 0) {
		buf[0] = 0;
		return;
	}
	snprintf(buf, n, "%s/Library/Application Support/sted/deffile", home);
}

static void
sf2_copy_path(char *dst, size_t n, const char *src)
{
	if (dst == NULL || n == 0)
		return;
	strncpy(dst, src, n - 1);
	dst[n - 1] = 0;
}

int
sf2_def_planned_out_path(const char *sf2_path, const char *out_dir,
			 char *out_path, size_t out_path_sz)
{
	char base[256], path[1024];

	if (sf2_path == NULL || out_dir == NULL || out_path == NULL || out_path_sz == 0)
		return SF2_DEF_ERR;
	sf2_sanitize_basename(base, sizeof base, sf2_path);
	snprintf(path, sizeof path, "%s/%s.def", out_dir, base);
	sf2_copy_path(out_path, out_path_sz, path);
	return SF2_DEF_OK;
}

int
sf2_def_generate(const char *sf2_path, const char *out_dir,
		 char *out_path, size_t out_path_sz,
		 Sf2DefGenResult *result,
		 char *errbuf, size_t errbuf_sz,
		 int overwrite)
{
	tsf *font;
	Sf2BankPresets *presets;
	int bankUsed[kMelodicBankMax + 1];
	int bank, prog, count, found;
	char base[256], module[64], id[64], path[1024], dirbuf[1024];
	char *p;
	struct stat st;
	struct Sf2RhyMap rhy;

	if (result != NULL) {
		result->preset_slots = 0;
		result->tsf_preset_count = 0;
	}
	if (sf2_path == NULL || out_dir == NULL) {
		sf2_set_err(errbuf, errbuf_sz, "invalid arguments");
		return SF2_DEF_ERR;
	}

	sf2_sanitize_basename(base, sizeof base, sf2_path);
	sf2_module_label(module, sizeof module, sf2_path);
	sf2_id_label(id, sizeof id, sf2_path);
	snprintf(path, sizeof path, "%s/%s.def", out_dir, base);
	sf2_copy_path(out_path, out_path_sz, path);

	if (!overwrite && stat(path, &st) == 0) {
		sf2_set_err(errbuf, errbuf_sz, "file already exists");
		return SF2_DEF_EXISTS;
	}

	memset(bankUsed, 0, sizeof bankUsed);
	presets = calloc((size_t)(kMelodicBankMax + 1), sizeof(Sf2BankPresets));
	if (presets == NULL) {
		sf2_set_err(errbuf, errbuf_sz, "out of memory");
		return SF2_DEF_ERR;
	}

	font = tsf_load_filename(sf2_path);
	if (font == NULL) {
		free(presets);
		sf2_set_err(errbuf, errbuf_sz, "Could not read the SF2 file");
		return SF2_DEF_ERR;
	}

	count = tsf_get_presetcount(font);
	found = 0;
	for (bank = 0; bank <= kMelodicBankMax; bank++) {
		for (prog = 0; prog < kProgCount; prog++) {
			const char *name = tsf_bank_get_presetname(font, bank, prog);
			if (name == NULL || name[0] == 0)
				continue;
			sf2_pad_name(presets[bank][prog], kToneNameWidth, name);
			bankUsed[bank] = 1;
			found++;
		}
	}
	tsf_close(font);

	if (found == 0) {
		free(presets);
		sf2_set_err(errbuf, errbuf_sz, "No presets found in the SoundFont");
		return SF2_DEF_ERR;
	}

	/* mkdir -p without shell */
	strncpy(dirbuf, out_dir, sizeof dirbuf - 1);
	dirbuf[sizeof dirbuf - 1] = 0;
	for (p = dirbuf + 1; *p != 0; p++) {
		if (*p == '/') {
			*p = 0;
			mkdir(dirbuf, 0755);
			*p = '/';
		}
	}
	mkdir(dirbuf, 0755);

	sf2_extract_rhythm(sf2_path, &rhy);
	if (sf2_write_def(path, module, id, presets, bankUsed, &rhy) != 0) {
		free(presets);
		sf2_set_err(errbuf, errbuf_sz, strerror(errno));
		return SF2_DEF_ERR;
	}
	free(presets);

	if (result != NULL) {
		result->preset_slots = found;
		result->tsf_preset_count = count;
	}
	return SF2_DEF_OK;
}
