#ifndef RCP_PLAYER_ENGINE_H
#define RCP_PLAYER_ENGINE_H

#include <stdint.h>

void rcpplayer_engine(void);
void rcpplayer_request_stop(void);
void rcpplayer_clear_stop(void);
void rcpplayer_reset_last_timestamp(void);
void rcpplayer_set_output_timing(int waitPerByteMicros, int offsetMicros);
void rcpplayer_record_note(int port, const uint8_t *msg, int length);
void rcpplayer_send_pending_note_offs(void);
void set_realtime_recording(int on);
int rcpplayer_is_realtime_recording(void);

#endif
