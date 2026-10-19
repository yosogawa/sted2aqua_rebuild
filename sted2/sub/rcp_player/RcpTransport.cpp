#include "RcpTransport.hpp"

extern "C" {
#include <stdint.h>
#include "../../rcddef.h"
#include "rcp_transport.h"
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
	setPhase(Phase::Running);
	*(volatile int32_t *)&rcd->sts = 0;
}

void
RcpTransport::continuePlay()
{
	if (!inService() || rcd == nullptr)
		return;
	setPhase(Phase::Running);
	*(volatile int32_t *)&rcd->sts = 0;
}

void
RcpTransport::stop()
{
	if (!inService() || rcd == nullptr)
		return;
	setPhase(Phase::Held);
	*(volatile int32_t *)&rcd->sts = 1;
}

void
RcpTransport::fullStop()
{
	if (!inService() || rcd == nullptr)
		return;
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

}
