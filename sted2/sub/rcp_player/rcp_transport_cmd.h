#ifndef RCP_TRANSPORT_CMD_H
#define RCP_TRANSPORT_CMD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Transport command queue and play launch helpers.
 *
 * MIDI callback: push only. Never start the engine here.
 *   FA/FB/FC → rcp_transport_cmd_push(status)
 *   F2 SPP   → assemble 3 bytes, then rcp_transport_cmd_push_spp(spp)
 * UI poll: rcp_transport_drain_commands().
 *   FA → request_start, FB → continue, FC → stop
 *   F2 → rcp_transport_song_position(spp)  (Locate: step/clock/seek)
 * UI play after packing: rcp_transport_launch_player()
 * begin/play_coremidi: detach only (no start()).
 *
 * 0xF8 is not queued — it goes straight to RcpClock (may interleave
 * inside an F2 message; the input assembler ignores realtime for SPP). */

typedef struct {
	uint8_t status; /* 0xFA, 0xFB, 0xFC, or 0xF2 */
	uint16_t spp;   /* 14-bit Song Position; valid when status == 0xF2 */
} RcpTransportCmd;

void rcp_transport_cmd_push(uint8_t status);
void rcp_transport_cmd_push_spp(uint16_t spp);
int rcp_transport_cmd_pop(RcpTransportCmd *out);
int rcp_transport_cmd_has(void);

void rcp_transport_drain_commands(void);

void rcp_transport_play_prepare_begin(void);
void rcp_transport_play_prepare_end(void);
int rcp_transport_play_prepare_busy(void);

int rcp_transport_play_image_ready(void);

void rcp_transport_launch_player(void);
void rcp_transport_request_start(void);

#ifdef __cplusplus
}
#endif

#endif
