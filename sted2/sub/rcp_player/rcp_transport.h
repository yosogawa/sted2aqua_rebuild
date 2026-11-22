#ifndef RCP_TRANSPORT_H
#define RCP_TRANSPORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Values match RcpTransport::Phase. */
enum {
	RCP_TRANSPORT_STOPPED = 0,
	RCP_TRANSPORT_RUNNING = 1,
	RCP_TRANSPORT_HELD = 2,
	RCP_TRANSPORT_SEARCH = 3
};

void rcp_transport_set_valid(void);
void rcp_transport_clear_valid(void);
void rcp_transport_attach(void);
void rcp_transport_set_active(int on);
int rcp_transport_is_active(void);
void rcp_transport_start(void);
void rcp_transport_continue(void);
void rcp_transport_stop(void);
void rcp_transport_full_stop(void);
/* Stop a live player, drop any Song Position locate, and set the locator
   to the top. The next Start plays from the beginning. */
void rcp_transport_return_to_start(void);
void rcp_transport_search(int track, int bar);
/* MIDI Song Position Pointer (0xF2), 14-bit. Stopped/Held: updates
   stepcount, RcpClock, and the player read position without Search.
   Recording / Running / Search: stores the value only. INT ignores F2
   in the command drain before this runs. */
void rcp_transport_song_position(uint16_t spp);
/* Song step where the current play image starts. 0 starts at the top.
   There is no getter: locate_spp() is the value MIDI out and the locator use. */
void rcp_transport_set_play_origin(int32_t step);
/* Current location as a 14-bit Song Position Pointer.
   (play origin + stepcount) * 4 / tbase. Safe to send as F2. */
uint16_t rcp_transport_locate_spp(void);
int rcp_transport_get_phase(void);
uint16_t rcp_transport_last_song_position(void);
/* Engine entry after Start: 1 and fills *us when SPP Locate set a start
   position; 0 otherwise (elapsed stays at resetAndRun zero). */
int rcp_transport_take_start_elapsed_us(double *us);

/* Musical elapsed time (RcpClock). Transport start() prepares the player
   from rcd when data_valid (measure table, rcpplayer_init; stops a live
   engine if any), then clears the clock and leaves it stopped — unless
   an SPP Locate is pending, in which case elapsed is restored at engine
   entry via take_start_elapsed_us. Prep is skipped when the image is
   missing; stop is skipped when no engine is live. The phase change
   still runs after a skipped prep. PAUSE and full stop freeze it.
   CONTINUE resumes it. Search resumes it so a search that follows
   PAUSE keeps playing.
   INT: host-time while armed. EXT: F8 is queued; the sequencer takes one
   timebase tick at a time (F8 is tick 1). arm/resume does not advance
   until that tick is taken (Continue semantics).

   Play launch lives in rcp_transport_cmd.h:
   request_start (FA / cold) and launch_player (UI after pack).
   begin/play_coremidi only detaches the engine thread. */
void rcp_sequence_clock_reset_and_run(void);
void rcp_sequence_clock_stop(void);
double rcp_sequence_clock_elapsed_us(void);
void rcp_sequence_clock_set_elapsed_us(double us);

/* RcpClock source / EXT helpers (0 = INT, nonzero = EXT). */
void rcp_clock_set_source(int ext);
int rcp_clock_get_source(void);
void rcp_clock_on_timing_clock(void);
/* 1 when EXT musical time advanced by one timebase tick. */
int rcp_clock_take_ext_tick(void);
/* Sleep this long when rcp_clock_take_ext_tick returned 0. 0 means due now. */
int rcp_clock_ext_wait_us(void);
void rcp_clock_set_tempo(double bpm, int timebase);
void rcp_clock_set_tempo_us(int32_t quarter_us, int timebase);

/* F8 diagnostics (reset on resetAndRun).
   The stderr and /tmp/sted_rcp_clock.log write is compiled only when
   MIDI_CLOCK_DEBUG is defined. */
void rcp_clock_dump_f8_stats(const char *tag);
uint64_t rcp_clock_f8_rx(void);
uint64_t rcp_clock_f8_applied(void);

#ifdef __cplusplus
}
#endif

#endif
