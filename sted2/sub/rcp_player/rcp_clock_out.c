#include "rcp_clock_out.h"

#include "rcp_player.h"
#include "rcp_transport.h"

#include "sted.h"

#include <CoreAudio/CoreAudio.h>
#include <CoreMIDI/CoreMIDI.h>
#include <stdatomic.h>

/* One clock sits on the wire with a 3ms delivery margin. The next one
   stays in this module until that stamp has passed. */
#define CLOCK_MARGIN_US 3000.0

#define CLOCK_PORTS 2

extern OSStatus CoreMIDISend(uint8_t *ptr, int length, int port, MIDITimeStamp atTime);
extern int MIDISettingsOutputClock(int port);
extern int is_coremidi_playing(void);

typedef struct ClockPort {
	MIDITimeStamp last;
	MIDITimeStamp last_f8;
	int live;
	int generation;
} ClockPort;

static ClockPort sPort[CLOCK_PORTS];
static int sSession;
static int sGeneration;
static int sArmed;
static int sInFlight;
static MIDITimeStamp sInFlightStamp;
static int64_t sNextClock;
static atomic_int sPendingSpp;

static int
clock_int(void)
{
	return rcp_clock_get_source() == 0;
}

static int
port_wants_clock(int port)
{
	return MIDISettingsOutputClock(port) ? 1 : 0;
}

static MIDITimeStamp
host_after_us(MIDITimeStamp base, double us)
{
	if (us <= 0.0)
		return base;
	return base + AudioConvertNanosToHostTime((UInt64)(us * 1000.0));
}

static MIDITimeStamp
stamp_after(int port, MIDITimeStamp want)
{
	MIDITimeStamp now;

	now = AudioGetCurrentHostTime();
	if (want < now)
		want = now;
	if (sPort[port].last != 0 && want <= sPort[port].last)
		want = sPort[port].last + 1;
	return want;
}

static void
emit(int port, const uint8_t *bytes, int length, MIDITimeStamp want, int is_f8)
{
	MIDITimeStamp at;

	at = stamp_after(port, want);
	CoreMIDISend((uint8_t *)bytes, length, port, at);
	sPort[port].last = at;
	if (is_f8)
		sPort[port].last_f8 = at;
	else if (bytes[0] == 0xFC)
		sPort[port].last_f8 = 0;
}

static void
send_status(int port, uint8_t status, MIDITimeStamp want)
{
	uint8_t b;

	b = status;
	emit(port, &b, 1, want, status == 0xF8);
}

static void
send_spp(int port, uint16_t spp, MIDITimeStamp want)
{
	uint8_t b[3];

	b[0] = 0xF2;
	b[1] = (uint8_t)(spp & 0x7f);
	b[2] = (uint8_t)((spp >> 7) & 0x7f);
	emit(port, b, 3, want, 0);
}

static int32_t
image_step_now(void)
{
	double elapsed;
	int32_t step;

	elapsed = rcp_sequence_clock_elapsed_us();
	if (elapsed <= 0.0)
		return 0;
	step = rcpplayer_microseconds_to_step((uint32_t)elapsed);
	if (step < 0)
		step = 0;
	return step;
}

static uint16_t
song_spp(void)
{
	int tb;
	int64_t song;

	tb = tbase;
	if (tb <= 0)
		tb = rcpplayer_timebase();
	if (tb <= 0)
		return 0;
	song = (int64_t)rcp_transport_play_origin() + (int64_t)image_step_now();
	if (song < 0)
		song = 0;
	return (uint16_t)(((song * 4) / (int64_t)tb) & 0x3fff);
}

static double
image_us_at(double step)
{
	int32_t base;
	double us0, us1;

	if (step <= 0.0)
		return rcpplayer_step_to_microseconds(0);
	base = (int32_t)step;
	us0 = rcpplayer_step_to_microseconds(base);
	us1 = rcpplayer_step_to_microseconds(base + 1);
	return us0 + (step - (double)base) * (us1 - us0);
}

static void
arm_grid(void)
{
	int tb;
	int64_t song;

	tb = rcpplayer_timebase();
	sInFlight = 0;
	sInFlightStamp = 0;
	sNextClock = 1;
	if (tb <= 0)
		return;
	song = (int64_t)rcp_transport_play_origin() + (int64_t)image_step_now();
	if (song < 0)
		song = 0;
	sNextClock = (song * 24) / (int64_t)tb + 1;
}

static void
open_port(int port, int cont)
{
	uint16_t spp;
	MIDITimeStamp now;
	int same;

	/* same generation: this port already followed the play, so resume
	   is Continue alone. A new play, or a port that just turned on,
	   gets Start or Song Position + Continue. */
	same = cont && sPort[port].generation == sGeneration && sGeneration != 0;
	now = AudioGetCurrentHostTime();
	spp = song_spp();
	if (same) {
		send_status(port, 0xFB, now);
	} else if (!cont && spp == 0) {
		send_status(port, 0xFA, now);
	} else if (spp == 0) {
		send_status(port, 0xFB, now);
	} else {
		send_spp(port, spp, now);
		send_status(port, 0xFB, now);
	}
	sPort[port].generation = sGeneration;
	sPort[port].live = 1;
}

static void
close_port(int port)
{
	MIDITimeStamp now;
	MIDITimeStamp tail;

	if (!sPort[port].live)
		return;
	now = AudioGetCurrentHostTime();
	tail = sPort[port].last_f8;
	if (tail == 0)
		tail = sPort[port].last;
	if (tail < now)
		tail = now;
	else
		tail = tail + 1;
	send_status(port, 0xFC, tail);
	sPort[port].live = 0;
}

static void
silence(void)
{
	int port;

	for (port = 0; port < CLOCK_PORTS; port++)
		close_port(port);
	sArmed = 0;
	sInFlight = 0;
	sInFlightStamp = 0;
}

static void
open_enabled(int cont)
{
	int port;

	for (port = 0; port < CLOCK_PORTS; port++) {
		if (!port_wants_clock(port)) {
			close_port(port);
			continue;
		}
		if (!sPort[port].live)
			open_port(port, cont);
	}
	sArmed = 1;
	arm_grid();
}

static void
sync_live_ports(void)
{
	int port;

	for (port = 0; port < CLOCK_PORTS; port++) {
		if (port_wants_clock(port)) {
			if (!sPort[port].live)
				open_port(port, 1);
		} else if (sPort[port].live) {
			close_port(port);
		}
	}
}

static void
publish_spp(void)
{
	uint16_t spp;
	MIDITimeStamp now;
	int port;

	spp = song_spp();
	now = AudioGetCurrentHostTime();
	for (port = 0; port < CLOCK_PORTS; port++) {
		if (!port_wants_clock(port))
			continue;
		send_spp(port, spp, now);
	}
}

void
rcp_clock_out_begin(void)
{
	sSession = 1;
	sGeneration++;
	if (!clock_int())
		return;
	open_enabled(0);
}

void
rcp_clock_out_continue(void)
{
	int pending;

	sSession = 1;
	if (!clock_int())
		return;
	/* Locate during pause is applied before Continue, on this thread. */
	pending = atomic_exchange(&sPendingSpp, 0);
	if (pending)
		publish_spp();
	open_enabled(1);
}

void
rcp_clock_out_end(void)
{
	silence();
	sSession = 0;
}

static int
clock_due(MIDITimeStamp *stamp)
{
	int tb;
	double song_step, image_step, target_us, now_us, delta;
	MIDITimeStamp now, grid;

	tb = rcpplayer_timebase();
	if (tb <= 0 || sNextClock <= 0)
		return 0;
	song_step = (double)sNextClock * (double)tb / 24.0;
	image_step = song_step - (double)rcp_transport_play_origin();
	target_us = image_us_at(image_step);
	now_us = rcp_sequence_clock_elapsed_us();
	now = AudioGetCurrentHostTime();
	delta = target_us - now_us;
	if (delta > 0.0)
		grid = host_after_us(now, delta);
	else
		grid = now;
	if (grid > now)
		return 0;
	*stamp = host_after_us(now, CLOCK_MARGIN_US);
	return 1;
}

static void
submit_one_clock(void)
{
	MIDITimeStamp stamp, at, latest;
	int port, sent;
	uint8_t b;

	if (sInFlight && AudioGetCurrentHostTime() < sInFlightStamp)
		return;
	sInFlight = 0;
	if (!clock_due(&stamp))
		return;
	b = 0xF8;
	sent = 0;
	latest = 0;
	for (port = 0; port < CLOCK_PORTS; port++) {
		if (!sPort[port].live)
			continue;
		at = stamp_after(port, stamp);
		CoreMIDISend(&b, 1, port, at);
		sPort[port].last = at;
		sPort[port].last_f8 = at;
		if (at > latest)
			latest = at;
		sent = 1;
	}
	if (!sent)
		return;
	sInFlight = 1;
	sInFlightStamp = latest;
	sNextClock++;
}

void
rcp_clock_out_service(void)
{
	if (!sSession)
		return;
	if (!clock_int()) {
		silence();
		return;
	}
	if (!sArmed)
		open_enabled(1);
	else
		sync_live_ports();
	if (sArmed)
		submit_one_clock();
}

void
rcp_clock_out_flush_spp(void)
{
	int pending;

	pending = atomic_exchange(&sPendingSpp, 0);
	if (!pending || !clock_int())
		return;
	/* Held: clocks are already stopped. Publish the new start position. */
	publish_spp();
}

void
rcp_clock_out_position_changed(void)
{
	if (!clock_int())
		return;
	if (is_coremidi_playing()) {
		atomic_store(&sPendingSpp, 1);
		return;
	}
	publish_spp();
}
