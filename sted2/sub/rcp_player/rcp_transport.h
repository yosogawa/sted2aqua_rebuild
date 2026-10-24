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
void rcp_transport_search(int track, int bar);
int rcp_transport_get_phase(void);

/* Musical elapsed time (RcpClock). Transport start() clears it and leaves
   it stopped. The player starts it at engine entry, which is elapsed zero.
   PAUSE and full stop freeze it. CONTINUE resumes it. Search resumes it so
   a search that follows PAUSE keeps playing.
   INT: host-time while armed. EXT: MIDI Timing Clock (0xF8) while armed;
   arm/resume does not advance until the first F8 (Continue semantics). */
void rcp_sequence_clock_reset_and_run(void);
void rcp_sequence_clock_stop(void);
double rcp_sequence_clock_elapsed_us(void);

/* RcpClock source / EXT helpers (0 = INT, nonzero = EXT). */
void rcp_clock_set_source(int ext);
int rcp_clock_get_source(void);
void rcp_clock_on_timing_clock(void);
void rcp_clock_set_tempo(double bpm, int timebase);
void rcp_clock_set_tempo_us(int32_t quarter_us, int timebase);

/* F8 diagnostics (reset on resetAndRun). */
void rcp_clock_dump_f8_stats(const char *tag);
uint64_t rcp_clock_f8_rx(void);
uint64_t rcp_clock_f8_applied(void);

#ifdef __cplusplus
}
#endif

#endif
