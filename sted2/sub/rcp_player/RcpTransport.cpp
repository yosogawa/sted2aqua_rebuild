#include "RcpTransport.hpp"
#include "RcpClock.hpp"

extern "C" {
#include <stdint.h>
#include "../../rcddef.h"
#include "rcp_transport.h"
#include "rcp_player.h"
#include "rcp_player_engine.h"

int STed_InitMeasureConversion(void);
int is_coremidi_playing(void);
void stop_coremidi_rcpplayer(void);
extern int tbase;
}

namespace {

RcpTransport gTransport;

}

static_assert(static_cast<int>(RcpTransport::Phase::Stopped) == RCP_TRANSPORT_STOPPED, "Stopped");
static_assert(static_cast<int>(RcpTransport::Phase::Running) == RCP_TRANSPORT_RUNNING, "Running");
static_assert(static_cast<int>(RcpTransport::Phase::Held) == RCP_TRANSPORT_HELD, "Held");
static_assert(static_cast<int>(RcpTransport::Phase::Search) == RCP_TRANSPORT_SEARCH, "Search");

RcpTransport::RcpTransport()
	: valid_(false)
	, phase_(static_cast<int>(Phase::Stopped))
	, active_(0)
	, last_spp_(0)
	, spp_valid_(false)
	, start_elapsed_valid_(false)
	, start_elapsed_us_(0.0)
	, play_origin_(0)
{
}

void
RcpTransport::setValid()
{
	valid_.store(true, std::memory_order_release);
}

void
RcpTransport::clearValid()
{
	valid_.store(false, std::memory_order_release);
}

bool
RcpTransport::inService() const
{
	return valid_.load(std::memory_order_acquire);
}

void
RcpTransport::clearSongPositionLocate()
{
	spp_valid_.store(false, std::memory_order_release);
	start_elapsed_valid_.store(false, std::memory_order_release);
	start_elapsed_us_.store(0.0, std::memory_order_release);
	last_spp_.store(0, std::memory_order_release);
}

void
RcpTransport::applySongPositionLocate(uint16_t spp)
{
	int32_t step;
	int32_t landed;
	double us;

	if (rcd == nullptr || tbase <= 0)
		return;

	/* Beat-unit SPP is song-absolute. stepcount is an offset from the
	   image start, so seek that many steps into the current image. */
	step = (int32_t)(((int64_t)(spp & 0x3fff) * (int64_t)tbase) / 4);
	step -= play_origin_.load(std::memory_order_acquire);
	if (step < 0)
		step = 0;
	landed = step;

	if (rcd->data_valid && rcd->data_adr != nullptr && rcd->totalcount > 0) {
		/* Seed the player when Start has not run yet, then seek.
		   Past-end SPP clamps to the last event (seek_to_step). */
		(void)rcpplayer_init(rcd->data_adr, (size_t)rcd->totalcount);
		(void)rcpplayer_seek_to_step(step, &landed);
		us = rcpplayer_step_to_microseconds(landed);
	} else {
		/* No play image yet: stepcount + approximate clock only. */
		us = 0.0;
		if (tbase > 0)
			us = (double)step * (500000.0 / (double)tbase);
		landed = step;
	}

	/* Real step only — never a fake bar/step, and never past EOF. */
	rcd->stepcount = landed;
	gRcpClock.setElapsedUs(us);
	start_elapsed_us_.store(us, std::memory_order_release);
	start_elapsed_valid_.store(true, std::memory_order_release);
	rcpplayer_invalidate_park();
}

void
RcpTransport::attach()
{
	if (rcd == nullptr)
		return;
	setPhase(Phase::Stopped);
	active_.store(0, std::memory_order_release);
	clearSongPositionLocate();
	gRcpClock.resetStopped();
	rcd->act = 0;
	*(volatile int32_t *)&rcd->sts = 1;
	rcd->tar_trk = 0;
	rcd->tar_bar = 0;
	play_origin_.store(0, std::memory_order_release);
}

void
RcpTransport::setActive(int on)
{
	int value = on ? 1 : 0;

	if (rcd == nullptr)
		return;
	active_.store(value, std::memory_order_release);
	rcd->act = value;
}

int
RcpTransport::active() const
{
	return active_.load(std::memory_order_acquire);
}

void
RcpTransport::setPhase(Phase phase)
{
	phase_.store(static_cast<int>(phase), std::memory_order_release);
}

RcpTransport::Phase
RcpTransport::phase() const
{
	return static_cast<Phase>(phase_.load(std::memory_order_acquire));
}

void
RcpTransport::start()
{
	/* 7: refuse while the gate is closed or rcd is missing. */
	if (!inService() || rcd == nullptr)
		return;
	/* 8: Search is set before begin(). Starting must not clear it. */
	if (phase() == Phase::Search)
		return;

	/* 6: a live engine must exit before this Start owns the player.
	   Skip when the thread is already down. */
	if (is_coremidi_playing())
		stop_coremidi_rcpplayer();

	/* 4, 5: prepare the player from rcd when a playable image exists.
	   Missing data skips prep; the phase change below still runs.
	   A pending SPP re-applies Locate after init so FA follows F2.
	   Measure tables belong here (once per Start), not on every F2. */
	if (rcd->data_valid && rcd->data_adr != nullptr && rcd->totalcount > 0)
		(void)STed_InitMeasureConversion();

	if (spp_valid_.load(std::memory_order_acquire)) {
		applySongPositionLocate(last_spp_.load(std::memory_order_acquire));
	} else if (rcd->data_valid && rcd->data_adr != nullptr && rcd->totalcount > 0) {
		(void)rcpplayer_init(rcd->data_adr, (size_t)rcd->totalcount);
		gRcpClock.resetStopped();
		start_elapsed_valid_.store(false, std::memory_order_release);
	} else {
		gRcpClock.resetStopped();
		start_elapsed_valid_.store(false, std::memory_order_release);
	}

	setPhase(Phase::Running);
	*(volatile int32_t *)&rcd->sts = 0;
}

void
RcpTransport::continuePlay()
{
	if (!inService() || rcd == nullptr)
		return;
	gRcpClock.resume();
	setPhase(Phase::Running);
	*(volatile int32_t *)&rcd->sts = 0;
}

void
RcpTransport::stop()
{
	if (!inService() || rcd == nullptr)
		return;
	gRcpClock.stop();
	setPhase(Phase::Held);
	*(volatile int32_t *)&rcd->sts = 1;
}

void
RcpTransport::fullStop()
{
	if (!inService() || rcd == nullptr)
		return;
	gRcpClock.stop();
	/* Keep last SPP across engine teardown. FA → all_note_off → end →
	   fullStop must not erase F2 that arrived before Start. attach()
	   clears it for a fresh session. */
	start_elapsed_valid_.store(false, std::memory_order_release);
	setPhase(Phase::Stopped);
	*(volatile int32_t *)&rcd->sts = 1;
}

void
RcpTransport::returnToStart()
{
	int i;

	if (!inService() || rcd == nullptr)
		return;
	/* fullStop keeps the locate. A rebuilt synth has lost every program
	   change already sent, so the next Start must begin at step 0. */
	if (is_coremidi_playing())
		stop_coremidi_rcpplayer();
	else
		fullStop();
	clearSongPositionLocate();
	gRcpClock.resetStopped();
	play_origin_.store(0, std::memory_order_release);
	rcd->stepcount = 0;
	for (i = 0; i < TRK_NUM; i++) {
		rcd->bar[i] = 0;
		rcd->step[i] = 0;
		rcd->active[i] = 1;
	}
	rcd->tar_trk = 0;
	rcd->tar_bar = 0;
	setActive(0);
	setPhase(Phase::Stopped);
	*(volatile int32_t *)&rcd->sts = 1;
	rcpplayer_invalidate_park();
}

void
RcpTransport::search(int track, int bar)
{
	if (!inService() || rcd == nullptr)
		return;
	rcd->tar_trk = track;
	rcd->tar_bar = bar;
	/* Search keeps playing. resume() is a no-op while the clock is already running. */
	gRcpClock.resume();
	setPhase(Phase::Search);
	*(volatile int32_t *)&rcd->sts = 2;
}

void
RcpTransport::songPosition(uint16_t spp)
{
	Phase p;

	if (!inService())
		return;
	spp = static_cast<uint16_t>(spp & 0x3fff);
	last_spp_.store(spp, std::memory_order_release);
	spp_valid_.store(true, std::memory_order_release);

	/* Recording / count-in: store only. */
	if (rcpplayer_is_realtime_recording())
		return;

	p = phase();
	if (p != Phase::Stopped && p != Phase::Held)
		return;

	applySongPositionLocate(spp);
}

uint16_t
RcpTransport::lastSongPosition() const
{
	return last_spp_.load(std::memory_order_acquire);
}

void
RcpTransport::setPlayOrigin(int32_t step)
{
	if (step < 0)
		step = 0;
	play_origin_.store(step, std::memory_order_release);
}

uint16_t
RcpTransport::locateSpp() const
{
	int32_t step;
	int64_t song;

	if (rcd == nullptr || tbase <= 0)
		return 0;
	step = rcd->stepcount;
	if (step < 0)
		step = 0;
	song = (int64_t)play_origin_.load(std::memory_order_acquire) + (int64_t)step;
	if (song < 0)
		song = 0;
	return (uint16_t)(((song * 4) / (int64_t)tbase) & 0x3fff);
}

bool
RcpTransport::takeStartElapsedUs(double *us)
{
	if (!start_elapsed_valid_.exchange(false, std::memory_order_acq_rel))
		return false;
	if (us != nullptr)
		*us = start_elapsed_us_.load(std::memory_order_acquire);
	return true;
}

extern "C" {

void
rcp_transport_set_valid(void)
{
	gTransport.setValid();
}

void
rcp_transport_clear_valid(void)
{
	gTransport.clearValid();
}

void
rcp_transport_attach(void)
{
	gTransport.attach();
}

void
rcp_transport_set_active(int on)
{
	gTransport.setActive(on);
}

int
rcp_transport_is_active(void)
{
	return gTransport.active();
}

void
rcp_transport_start(void)
{
	gTransport.start();
}

void
rcp_transport_continue(void)
{
	gTransport.continuePlay();
}

void
rcp_transport_stop(void)
{
	gTransport.stop();
}

void
rcp_transport_full_stop(void)
{
	gTransport.fullStop();
}

void
rcp_transport_return_to_start(void)
{
	gTransport.returnToStart();
}

void
rcp_transport_search(int track, int bar)
{
	gTransport.search(track, bar);
}

void
rcp_transport_song_position(uint16_t spp)
{
	gTransport.songPosition(spp);
}

int
rcp_transport_get_phase(void)
{
	return static_cast<int>(gTransport.phase());
}

uint16_t
rcp_transport_last_song_position(void)
{
	return gTransport.lastSongPosition();
}

void
rcp_transport_set_play_origin(int32_t step)
{
	gTransport.setPlayOrigin(step);
}

uint16_t
rcp_transport_locate_spp(void)
{
	return gTransport.locateSpp();
}

int
rcp_transport_take_start_elapsed_us(double *us)
{
	return gTransport.takeStartElapsedUs(us) ? 1 : 0;
}

}
