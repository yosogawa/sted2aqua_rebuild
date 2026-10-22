#include "RcpTransport.hpp"

#include <CoreAudio/CoreAudio.h>
#include <mutex>

extern "C" {
#include <stdint.h>
#include "../../rcddef.h"
#include "rcp_transport.h"
}

namespace {

/* Elapsed host ticks since this play's engine entry, excluding PAUSE.
   A read while running adds the host delta since the current segment.
   There is no separate tick. */
class SequenceClock {
public:
	SequenceClock()
		: running_(false)
		, accumulated_(0)
		, segmentStart_(0)
	{
	}

	void resetStopped()
	{
		std::lock_guard<std::mutex> lock(mu_);
		running_ = false;
		accumulated_ = 0;
		segmentStart_ = 0;
	}

	void resetAndRun()
	{
		std::lock_guard<std::mutex> lock(mu_);
		accumulated_ = 0;
		segmentStart_ = AudioGetCurrentHostTime();
		running_ = true;
	}

	void stop()
	{
		std::lock_guard<std::mutex> lock(mu_);
		if (!running_)
			return;
		uint64_t now = AudioGetCurrentHostTime();
		if (now > segmentStart_)
			accumulated_ += now - segmentStart_;
		running_ = false;
	}

	void resume()
	{
		std::lock_guard<std::mutex> lock(mu_);
		if (running_)
			return;
		segmentStart_ = AudioGetCurrentHostTime();
		running_ = true;
	}

	double elapsedUs()
	{
		std::lock_guard<std::mutex> lock(mu_);
		uint64_t ticks = accumulated_;
		if (running_) {
			uint64_t now = AudioGetCurrentHostTime();
			if (now > segmentStart_)
				ticks += now - segmentStart_;
		}
		return static_cast<double>(AudioConvertHostTimeToNanos(ticks)) / 1000.0;
	}

private:
	std::mutex mu_;
	bool running_;
	uint64_t accumulated_;
	uint64_t segmentStart_;
};

/* HWクロックのミラーだが、PAUSEでは止まる。 */
SequenceClock gSequenceClock;
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
RcpTransport::attach()
{
	if (rcd == nullptr)
		return;
	setPhase(Phase::Stopped);
	active_.store(0, std::memory_order_release);
	gSequenceClock.resetStopped();
	rcd->act = 0;
	*(volatile int32_t *)&rcd->sts = 1;
	rcd->tar_trk = 0;
	rcd->tar_bar = 0;
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
	if (!inService() || rcd == nullptr)
		return;
	/* Search is set before begin(). Starting must not clear it. */
	if (phase() == Phase::Search)
		return;
	/* Elapsed zero is engine entry, not this call. Leave the clock stopped
	   so the gap until the player thread starts is not musical time. */
	gSequenceClock.resetStopped();
	setPhase(Phase::Running);
	*(volatile int32_t *)&rcd->sts = 0;
}

void
RcpTransport::continuePlay()
{
	if (!inService() || rcd == nullptr)
		return;
	gSequenceClock.resume();
	setPhase(Phase::Running);
	*(volatile int32_t *)&rcd->sts = 0;
}

void
RcpTransport::stop()
{
	if (!inService() || rcd == nullptr)
		return;
	gSequenceClock.stop();
	setPhase(Phase::Held);
	*(volatile int32_t *)&rcd->sts = 1;
}

void
RcpTransport::fullStop()
{
	if (!inService() || rcd == nullptr)
		return;
	gSequenceClock.stop();
	setPhase(Phase::Stopped);
	*(volatile int32_t *)&rcd->sts = 1;
}

void
RcpTransport::search(int track, int bar)
{
	if (!inService() || rcd == nullptr)
		return;
	rcd->tar_trk = track;
	rcd->tar_bar = bar;
	/* Search keeps playing. resume() is a no-op while the clock is already running. */
	gSequenceClock.resume();
	setPhase(Phase::Search);
	*(volatile int32_t *)&rcd->sts = 2;
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
rcp_transport_search(int track, int bar)
{
	gTransport.search(track, bar);
}

int
rcp_transport_get_phase(void)
{
	return static_cast<int>(gTransport.phase());
}

void
rcp_sequence_clock_reset_and_run(void)
{
	gSequenceClock.resetAndRun();
}

void
rcp_sequence_clock_stop(void)
{
	gSequenceClock.stop();
}

double
rcp_sequence_clock_elapsed_us(void)
{
	return gSequenceClock.elapsedUs();
}

}
