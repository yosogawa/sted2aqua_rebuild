/*
 *  rcp_player.c
 *  STed2_aqua
 *
 *  Created by Toshi Nagata on Thu Jun 05 2003.
 *  Copyright (c) 2003 Toshi Nagata. All rights reserved.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include "rcp_player.h"

#include "sted.h"
#include "rcp.h"
#include "rcp_functions.h"
#include "rcpconv.h"
#include "smf.h"
#include "rcp_transport.h"

/*
 * Temporary full dump for A/B port bleed debugging.
 * Writes /tmp/sted2_rcp_midi_dump.txt (and mirrors to stderr).
 * Set to 0 when done investigating.
 */
#ifndef RCP_MIDI_PORT_DUMP
#define RCP_MIDI_PORT_DUMP 0
#endif

/*  Functions in rcptomid.c  */
extern int rcptomid_init_track_buffer( RCP_DATA * );
extern int rcptomid_read_rcp_header( RCP_DATA * );
extern int rcptomid_set_new_event( RCP_DATA *, int );
/* int rcp_note_off(RCP_DATA *rcp, int track, int note ); */

/*int output_midi_event( RCP_DATA *, int );
int set_midi_tempo( RCP_DATA * ); */

typedef struct rcpplayer_record {
	RCP_DATA rcp;
	int32_t total_step;
	int delta_step;
	int smf_length;
	int next_track;
	int next_note;
	uint8_t result_smf[kMaxMIDIMessageLength];
	
	/*  Tempo history  */
	int32_t last_tempo_step;
	double last_tempo_us;
	int32_t last_tempo;
} RCPPLAYER;

static RCPPLAYER sPlayer;

#if RCP_MIDI_PORT_DUMP
static FILE *sMidiDumpFp;

static void
rcp_midi_dump_close(void)
{
	if (sMidiDumpFp != NULL) {
		fclose(sMidiDumpFp);
		sMidiDumpFp = NULL;
	}
}

static void
rcp_midi_dumpf(const char *fmt, ...)
{
	va_list ap, ap2;
	va_start(ap, fmt);
	va_copy(ap2, ap);
	vfprintf(stderr, fmt, ap);
	if (sMidiDumpFp != NULL) {
		vfprintf(sMidiDumpFp, fmt, ap2);
		fflush(sMidiDumpFp);
	}
	va_end(ap2);
	va_end(ap);
}

static void
rcp_midi_dump_open(void)
{
	time_t now;
	rcp_midi_dump_close();
	sMidiDumpFp = fopen("/tmp/sted2_rcp_midi_dump.txt", "w");
	now = time(NULL);
	rcp_midi_dumpf("==== STed2 RCP MIDI dump %s", ctime(&now));
	if (sMidiDumpFp == NULL)
		rcp_midi_dumpf("(file open failed; stderr only)\n");
	else
		rcp_midi_dumpf("file=/tmp/sted2_rcp_midi_dump.txt\n");
}

static void
rcp_midi_dump_tracks(const char *phase)
{
	int t, raw;
	const uint8_t *ptr;

	rcp_midi_dumpf("-- tracks @ %s  n=%d steddata=%d\n",
		phase, sPlayer.rcp.tracks, sPlayer.rcp.steddata);
	ptr = sPlayer.rcp.data;
	for (t = 0; t < sPlayer.rcp.tracks; t++) {
		raw = -1;
		if (ptr != NULL && sPlayer.rcp.track[t].top >= 0 &&
		    (size_t)(sPlayer.rcp.track[t].top + 4) < sPlayer.rcp.length)
			raw = ptr[sPlayer.rcp.track[t].top + 4];
		rcp_midi_dumpf(
			"  tr=%02d en=%d fin=%d port=%d ch=%d mode=%d raw_hed4=0x%02x(%d) key=%d st=%d\n",
			t,
			sPlayer.rcp.track[t].enabled,
			sPlayer.rcp.track[t].finished,
			sPlayer.rcp.track[t].port,
			sPlayer.rcp.track[t].midi_ch,
			sPlayer.rcp.track[t].mode,
			raw & 0xff, raw,
			sPlayer.rcp.track[t].key,
			sPlayer.rcp.track[t].st);
	}
}

static void
rcp_midi_dump_event(int32_t step, int tr, int port, const uint8_t *msg, int len)
{
	int i, ch;

	ch = (tr >= 0 && tr < sPlayer.rcp.tracks) ? sPlayer.rcp.track[tr].midi_ch : -1;
	rcp_midi_dumpf("step=%7ld tr=%02d port=%d midi_ch=%d evt=%02x gate=%d vel=%d |",
		(long)step, tr, port, ch,
		(tr >= 0 && tr < sPlayer.rcp.tracks) ? sPlayer.rcp.track[tr].event : -1,
		(tr >= 0 && tr < sPlayer.rcp.tracks) ? sPlayer.rcp.track[tr].gate : -1,
		(tr >= 0 && tr < sPlayer.rcp.tracks) ? sPlayer.rcp.track[tr].vel : -1);
	if (msg == NULL || len <= 0) {
		rcp_midi_dumpf(" (no midi)\n");
		return;
	}
	for (i = 0; i < len && i < 32; i++)
		rcp_midi_dumpf(" %02x", msg[i]);
	if (len > 32)
		rcp_midi_dumpf(" ...");
	rcp_midi_dumpf("\n");
}
#else
#define rcp_midi_dump_open() ((void)0)
#define rcp_midi_dump_close() ((void)0)
#define rcp_midi_dump_tracks(phase) ((void)0)
#define rcp_midi_dump_event(step, tr, port, msg, len) ((void)0)
#endif

/* One-shot unget for seek_to_step: the event at/after the target is
   returned by the next rcpplayer_next_midi_event() call. */
static int sUngetValid;
static int32_t sUngetStep;
static int sUngetLength;
static int sUngetPort;
static uint8_t sUngetMsg[kMaxMIDIMessageLength];

static void
rcpplayer_debug_print(void)
{
	int n, tr;
	printf("step %ld: ", (long)sPlayer.total_step);
	for (n = 0; n < 256; n++) {
		if (sPlayer.rcp.result_smf[n] == SMF_TERM)
			break;
		printf("%02x ", sPlayer.rcp.result_smf[n]);
	}
	printf("(track %d)\n", sPlayer.next_track);
	for (tr = 0; tr < sPlayer.rcp.tracks; tr++) {
		if (sPlayer.rcp.track[tr].finished == FLAG_TRUE)
			continue;
		printf("  Track %d: %d %d %d %d ", (int)tr,
			(int)sPlayer.rcp.track[tr].event,
			(int)sPlayer.rcp.track[tr].step,
			(int)sPlayer.rcp.track[tr].gate,
			(int)sPlayer.rcp.track[tr].vel);
		if (sPlayer.rcp.track[tr].all_notes_expired == FLAG_FALSE) {
			printf("gate ");
			for (n = sPlayer.rcp.track[tr].notes_min; n <= sPlayer.rcp.track[tr].notes_max; n++) {
					if (sPlayer.rcp.track[tr].notes[n] != RCP_NOTE_EXPIRED) {
						printf("%d(%d) ", 
							(int)sPlayer.rcp.track[tr].notes[n], (int)n);
					}
				}
		}
		printf("\n");
	}
}

double
rcpplayer_step_to_microseconds(int32_t step)
{
	return sPlayer.last_tempo_us + ((double)(step - sPlayer.last_tempo_step) / sPlayer.rcp.timebase) * sPlayer.last_tempo;
}

int32_t
rcpplayer_microseconds_to_step(uint32_t micros)
{
	int32_t step;
	if (sPlayer.last_tempo == 0 || sPlayer.rcp.timebase == 0)
		return 0;
	step = sPlayer.last_tempo_step + (int32_t)(((double)micros - sPlayer.last_tempo_us) * sPlayer.rcp.timebase / sPlayer.last_tempo);
	return step;
}

static void
decrement_step(int delta_step, int *min_track, int *min_note, int *min_step)
{
	int m_track, m_note, m_step, nstep, n, tr;
	m_track = -1;
	m_note = -1;
	m_step = 10000000;
	for (tr = 0; tr < sPlayer.rcp.tracks; tr++) {
		/*  Check the gate times  */
		if (sPlayer.rcp.track[tr].all_notes_expired == FLAG_FALSE) {
			for (n = sPlayer.rcp.track[tr].notes_min; n <= sPlayer.rcp.track[tr].notes_max; n++) {
					if (sPlayer.rcp.track[tr].notes[n] != RCP_NOTE_EXPIRED) {
						nstep = (sPlayer.rcp.track[tr].notes[n] -= delta_step);
						if (m_step > nstep) {
							m_track = tr;
							m_note = n;
							m_step = nstep;
						}
					}
				}
		}
		/*  Check the next event  */
		sPlayer.rcp.track[tr].delta_step += delta_step;
		sPlayer.rcp.track[tr].total_step += delta_step;
		if (sPlayer.rcp.track[tr].finished == FLAG_FALSE) {
			nstep = (sPlayer.rcp.track[tr].step -= delta_step);
			if (m_step > nstep) {
				m_track = tr;
				m_note = -1;
				m_step = nstep;
			}
		}
	}
	if (min_track != NULL)
		*min_track = m_track;
	if (min_note != NULL)
		*min_note = m_note;
	if (min_step != NULL)
		*min_step = m_step;
}

int
rcpplayer_next_midi_event(int32_t *step, uint8_t **ptr, int *length, int *port)
{
//	long s_step;
	uint8_t *s_ptr;
	int s_length, s_maxlen;
	int s_port;
	int emit_tr = -1;
	int n, min_step, min_track, min_note, nstep, tr;

	if (sUngetValid) {
		sUngetValid = 0;
		if (step != NULL)
			*step = sUngetStep;
		if (ptr != NULL)
			*ptr = sUngetMsg;
		if (length != NULL)
			*length = sUngetLength;
		if (port != NULL)
			*port = sUngetPort;
		rcp_midi_dump_event(sUngetStep, -1, sUngetPort, sUngetMsg, sUngetLength);
		return 1;
	}

	if (sPlayer.total_step >= 0) {
		sPlayer.total_step += sPlayer.delta_step;
//		s_step = sPlayer.total_step;
		sPlayer.rcp.result_smf[0] = SMF_TERM;
		emit_tr = sPlayer.next_track;
		/*  Is the next event note-off?  */
		if (sPlayer.next_note >= 0) {
			rcp_note_off(&sPlayer.rcp, sPlayer.next_track, sPlayer.next_note);
		} else {
			rcptomid_set_new_event(&sPlayer.rcp, sPlayer.next_track);
			/*  Increment the step time by delta_step, to compensate the decrement performed later  */
			sPlayer.rcp.track[sPlayer.next_track].step += sPlayer.delta_step;
			if ((n = sPlayer.rcp.track[sPlayer.next_track].event) < 0x80) {
				/*  Note on: also increment the gate time  */
				if (sPlayer.rcp.track[sPlayer.next_track].key != 0x80 ) {
					n = (n + sPlayer.rcp.play_bias + sPlayer.rcp.track[sPlayer.next_track].key) % 128;
				}
				if (sPlayer.rcp.track[sPlayer.next_track].notes[n] != RCP_NOTE_EXPIRED)
					sPlayer.rcp.track[sPlayer.next_track].notes[n] += sPlayer.delta_step;
			}
		}
		s_ptr = sPlayer.result_smf;
		s_maxlen = sizeof(sPlayer.result_smf) / sizeof(sPlayer.result_smf[0]);
		s_length = 0;		
		while (s_length < s_maxlen && sPlayer.rcp.result_smf[s_length] != SMF_TERM) {
			sPlayer.result_smf[s_length] = sPlayer.rcp.result_smf[s_length];
			s_length++;
		}
		s_port = sPlayer.rcp.track[sPlayer.next_track].port;
	} else {
//		s_step = 0;
		s_ptr = NULL;
		s_length = 0;
		s_port = 0;
		sPlayer.total_step = 0;
		sPlayer.delta_step = 1;
		sPlayer.next_track = -1;
	}
	
	/*  Decrement all step/gate times and prefetch the next event  */
	decrement_step(sPlayer.delta_step, &min_track, &min_note, &min_step);

	if (min_step < 0) {
		printf("Something wrong: min_step = %d\n", min_step);
	}
	
	if (min_track >= 0) {
//		sPlayer.next_step = sPlayer.rcp.track[sPlayer.next_track].total_step + min_step;
		sPlayer.delta_step = min_step;
	} else {
		sPlayer.delta_step = 1000000;
		return 0;	/* No more events */
	}

#if 0
	rcpplayer_debug_print();
#endif

	sPlayer.next_track = min_track;
	sPlayer.next_note = min_note;
	sPlayer.delta_step = min_step;
	sPlayer.smf_length = s_length;

	if (s_length == 6 && s_ptr[0] == MIDI_META && s_ptr[1] == META_TEMPO) {
		/*  Set MIDI tempo  */
		sPlayer.last_tempo_us = rcpplayer_step_to_microseconds(sPlayer.total_step);
		sPlayer.last_tempo_step = sPlayer.total_step;
		sPlayer.last_tempo = ((int32_t)s_ptr[3] << 16) + ((int32_t)s_ptr[4] << 8) + s_ptr[5];
		/* Keep RcpClock EXT F8→µs aligned with the same quarter length. */
		rcp_clock_set_tempo_us(sPlayer.last_tempo, sPlayer.rcp.timebase);
	}
	
	if (step != NULL)
		*step = sPlayer.total_step;
	if (ptr != NULL)
		*ptr = s_ptr;
	if (length != NULL)
		*length = s_length;
	if (port != NULL)
		*port = s_port;
	if (emit_tr >= 0)
		rcp_midi_dump_event(sPlayer.total_step, emit_tr, s_port, s_ptr, s_length);
	return 1;
}

int
rcpplayer_init(uint8_t *data_ptr, size_t data_length)
{
	int result;
	sUngetValid = 0;
	memset(&sPlayer, 0, sizeof(sPlayer));
	sPlayer.last_tempo_step = 0;
	sPlayer.last_tempo_us = 0.0;
	sPlayer.last_tempo = 500000;
	sPlayer.rcp.data = data_ptr;
	sPlayer.rcp.length = data_length;
	rcp_midi_dump_open();
	result = rcptomid_init_track_buffer(&sPlayer.rcp);
	if (result == 0)
		result = rcptomid_read_rcp_header(&sPlayer.rcp);
	if (result == 0) {
		rcp_midi_dump_tracks("after header");
		sPlayer.total_step = -1;
		rcpplayer_next_midi_event(NULL, NULL, NULL, NULL);
		rcp_clock_set_tempo_us(sPlayer.last_tempo, sPlayer.rcp.timebase);
#if RCP_MIDI_PORT_DUMP
		rcp_midi_dumpf("-- event stream start\n");
#endif
	} else {
#if RCP_MIDI_PORT_DUMP
		rcp_midi_dumpf("rcpplayer_init failed result=%d\n", result);
#endif
		rcp_midi_dump_close();
	}
	return result;
}

static void
rcpplayer_unget_event(int32_t step, uint8_t *ptr, int length, int port)
{
	if (length < 0)
		length = 0;
	if (length > (int)sizeof(sUngetMsg))
		length = (int)sizeof(sUngetMsg);
	sUngetStep = step;
	sUngetLength = length;
	sUngetPort = port;
	if (ptr != NULL && length > 0)
		memcpy(sUngetMsg, ptr, (size_t)length);
	else
		sUngetLength = 0;
	sUngetValid = 1;
}

int
rcpplayer_seek_to_step(int32_t target_step, int32_t *landed_step)
{
	uint8_t *data_ptr;
	size_t data_length;
	int32_t step;
	int32_t last_step;
	uint8_t *ptr;
	int length, port;
	int iters;
	/* Hard cap: protects the UI thread if a loop never finishes. */
	const int kMaxSeekEvents = 2000000;

	data_ptr = sPlayer.rcp.data;
	data_length = sPlayer.rcp.length;
	if (data_ptr == NULL || data_length == 0)
		return -1;
	if (rcpplayer_init(data_ptr, data_length) != 0)
		return -1;
	if (target_step <= 0) {
		if (landed_step != NULL)
			*landed_step = 0;
		return 0;
	}

	last_step = 0;
	iters = 0;
	/* Discard events strictly before the target. Put back the first
	   event at or after it so the next next_midi_event() returns it
	   (same contract as a fresh init). */
	while (rcpplayer_next_midi_event(&step, &ptr, &length, &port)) {
		last_step = step;
		if (step >= target_step) {
			rcpplayer_unget_event(step, ptr, length, port);
			if (landed_step != NULL)
				*landed_step = target_step;
			return 0;
		}
		if (++iters >= kMaxSeekEvents)
			break;
	}

	/* Past the end (or safety cap): the failing next_midi_event already
	   left delta_step at the million-step sentinel. Rebuild and park on
	   the last real event so Start never resumes from a destroyed parser. */
	if (rcpplayer_init(data_ptr, data_length) != 0)
		return -1;
	if (last_step <= 0) {
		if (landed_step != NULL)
			*landed_step = 0;
		return 0;
	}
	iters = 0;
	while (rcpplayer_next_midi_event(&step, &ptr, &length, &port)) {
		if (step >= last_step) {
			rcpplayer_unget_event(step, ptr, length, port);
			break;
		}
		if (++iters >= kMaxSeekEvents)
			break;
	}
	if (landed_step != NULL)
		*landed_step = last_step;
	return 0;
}
