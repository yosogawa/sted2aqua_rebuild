#include "rcp_transport_cmd.h"
#include "rcp_transport.h"

#include <stddef.h>

#include "../../rcddef.h"

extern struct RCD_HEAD *rcd;

/* Same shape as the MIDI byte ring: C-only SPSC, full drops, empty pop fails. */
#define RCP_TRANSPORT_CMD_RING_SIZE 64

static RcpTransportCmd sCmdRing[RCP_TRANSPORT_CMD_RING_SIZE];
static volatile int sCmdHead = 0;
static volatile int sCmdTail = 0;
static volatile int sPlayPrepareBusy = 0;

void
rcp_transport_cmd_push(uint8_t status)
{
	int next = (sCmdHead + 1) % RCP_TRANSPORT_CMD_RING_SIZE;
	RcpTransportCmd cmd;

	if (next == sCmdTail)
		return;
	cmd.status = status;
	cmd.spp = 0;
	sCmdRing[sCmdHead] = cmd;
	sCmdHead = next;
}

void
rcp_transport_cmd_push_spp(uint16_t spp)
{
	int next = (sCmdHead + 1) % RCP_TRANSPORT_CMD_RING_SIZE;
	RcpTransportCmd cmd;

	if (next == sCmdTail)
		return;
	cmd.status = 0xF2;
	cmd.spp = (uint16_t)(spp & 0x3fff);
	sCmdRing[sCmdHead] = cmd;
	sCmdHead = next;
}

int
rcp_transport_cmd_pop(RcpTransportCmd *out)
{
	if (out == NULL)
		return 0;
	if (sCmdHead == sCmdTail)
		return 0;
	*out = sCmdRing[sCmdTail];
	sCmdTail = (sCmdTail + 1) % RCP_TRANSPORT_CMD_RING_SIZE;
	return 1;
}

int
rcp_transport_cmd_has(void)
{
	return (sCmdHead != sCmdTail) ? 1 : 0;
}

extern int is_coremidi_playing(void);
extern void stop_coremidi_rcpplayer(void);
extern int rcp_build_play_image(void);

void
rcp_transport_play_prepare_begin(void)
{
	sPlayPrepareBusy = 1;
}

void
rcp_transport_play_prepare_end(void)
{
	sPlayPrepareBusy = 0;
}

int
rcp_transport_play_prepare_busy(void)
{
	return sPlayPrepareBusy ? 1 : 0;
}

int
rcp_transport_play_image_ready(void)
{
	if (rcd == NULL)
		return 0;
	if (!rcd->data_valid || rcd->data_adr == NULL || rcd->totalcount <= 0)
		return 0;
	return 1;
}

void
rcp_transport_launch_player(void)
{
	/* start() is a no-op while Search (must keep that phase). The engine
	   thread still starts — same as the old begin→play_coremidi path. */
	rcp_transport_start();
	if (!rcp_transport_play_image_ready())
		return;
	if (rcd == NULL || rcd->begin == NULL)
		return;
	(*rcd->begin)();
}

void
rcp_transport_request_start(void)
{
	(void)rcp_build_play_image();
	rcp_transport_launch_player();
}

void
rcp_transport_drain_commands(void)
{
	RcpTransportCmd cmd;

	if (sPlayPrepareBusy)
		return;

	while (rcp_transport_cmd_pop(&cmd)) {
		if (rcp_clock_get_source() == 0)
			continue;
		switch (cmd.status) {
		case 0xFA:
			/* Restart even from Held/Running so F2→FA after rewind works.
			   stop → fullStop keeps last SPP; request_start re-applies it. */
			if (is_coremidi_playing())
				stop_coremidi_rcpplayer();
			rcp_transport_request_start();
			break;
		case 0xFB:
			rcp_transport_continue();
			break;
		case 0xFC:
			rcp_transport_stop();
			break;
		case 0xF2:
			rcp_transport_song_position(cmd.spp);
			break;
		default:
			break;
		}
	}
}
