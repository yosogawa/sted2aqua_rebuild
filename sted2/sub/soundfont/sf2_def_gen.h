/*
 * sf2_def_gen.h — SF2 analysis → STed .def skeleton (portable C).
 *
 * TinySoundFont (third_party/TinySoundFont) is used for preset enumeration.
 * Bank 128 key names for #rhy_as are read from the SF2 preset data directly.
 * UI lives in Sf2DefGenerator (ObjC); this unit has no Cocoa dependency.
 */

#ifndef SF2_DEF_GEN_H
#define SF2_DEF_GEN_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	SF2_DEF_OK = 0,
	SF2_DEF_ERR = -1,
	SF2_DEF_EXISTS = -2	/* target .def exists and overwrite was false */
};

typedef struct Sf2DefGenResult {
	int preset_slots;	/* bank/prog slots that had a name */
	int tsf_preset_count;	/* TinySoundFont preset count */
} Sf2DefGenResult;

/* Typical macOS locations (HOME-based). buf may be NULL. */
void sf2_def_default_banks_dir(char *buf, size_t n);
void sf2_def_default_out_dir(char *buf, size_t n);

/* Path that generate() would write (no I/O). Returns 0, or SF2_DEF_ERR. */
int sf2_def_planned_out_path(const char *sf2_path, const char *out_dir,
			     char *out_path, size_t out_path_sz);

/*
 * Load sf2_path, write a .def under out_dir (created if needed).
 * On success: SF2_DEF_OK, fills *result and out_path.
 * If the target exists and overwrite is 0: SF2_DEF_EXISTS (out_path filled).
 * Other failures: SF2_DEF_ERR and a short message in errbuf.
 */
int sf2_def_generate(const char *sf2_path, const char *out_dir,
		     char *out_path, size_t out_path_sz,
		     Sf2DefGenResult *result,
		     char *errbuf, size_t errbuf_sz,
		     int overwrite);

#ifdef __cplusplus
}
#endif

#endif /* SF2_DEF_GEN_H */
