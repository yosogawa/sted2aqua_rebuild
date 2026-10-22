#ifndef RCP_TRANSPORT_H
#define RCP_TRANSPORT_H

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

/* Musical elapsed time for one play. Transport start() clears it and leaves
   it stopped. The player starts it at engine entry, which is elapsed zero.
   PAUSE and full stop freeze it. CONTINUE resumes it. Search resumes it so
   a search that follows PAUSE keeps playing. Elapsed microseconds advance
   only while it is running, by the host-time delta since it last started. */
void rcp_sequence_clock_reset_and_run(void);
void rcp_sequence_clock_stop(void);
double rcp_sequence_clock_elapsed_us(void);

#ifdef __cplusplus
}
#endif

#endif
