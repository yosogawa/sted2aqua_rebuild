#ifndef RCP_CLOCK_OUT_H
#define RCP_CLOCK_OUT_H

/* MIDI Clock master (INT only). Sequence realtime bytes go out beside the
   note scheduler: this module does not read or write note timestamps.
   Call begin/service/continue/end/flush from the player thread.
   position_changed may run on the command thread only after that thread
   has joined, or while the player thread is alive (it only sets a flag). */

void rcp_clock_out_begin(void);
void rcp_clock_out_service(void);
void rcp_clock_out_continue(void);
void rcp_clock_out_end(void);
void rcp_clock_out_flush_spp(void);
void rcp_clock_out_position_changed(void);

#endif
