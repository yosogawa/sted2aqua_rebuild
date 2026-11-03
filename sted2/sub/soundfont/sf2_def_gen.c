/*
 * sf2_def_gen.c — SF2 → DEF skeleton generator (C).
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

static int
sf2_write_def(const char *outPath, const char *moduleLabel, const char *idLabel,
	      Sf2BankPresets *presets, const int *bankUsed)
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
		"* Melodic banks map via #gs_pbank; percussion bank 128 is listed as kits only.\r\n"
		"* Rhythm key maps / refine mapping: TODO with UI.\r\n"
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

	fprintf(fp,
		"#rhy_as=10,0,-1,,127\r\n"
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
		"#defoff\r\n"
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
		sf2_set_err(errbuf, errbuf_sz, "TinySoundFont failed to load the SF2 file");
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

	if (sf2_write_def(path, module, id, presets, bankUsed) != 0) {
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
