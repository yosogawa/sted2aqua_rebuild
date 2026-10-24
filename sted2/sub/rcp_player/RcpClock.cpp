#include "RcpClock.hpp"

#include <CoreAudio/CoreAudio.h>
#include <cstdio>

extern "C" {
#include "rcp_transport.h"
}

RcpClock gRcpClock;

RcpClock::RcpClock()
	: source_(Source::Int)
	, running_(false)
	, armed_(false)
	, musical_us_(0.0)
	, segmentStart_(0)
	, qn_us_(500000)
	, timebase_(48)
	, f8_rx_(0)
	, f8_applied_(0)
{
}

void
RcpClock::freezeHostLocked()
{
	if (!running_)
		return;
	musical_us_ += hostDeltaUsLocked();
	running_ = false;
}

double
RcpClock::hostDeltaUsLocked() const
{
	uint64_t now = AudioGetCurrentHostTime();
	if (now <= segmentStart_)
		return 0.0;
	return static_cast<double>(AudioConvertHostTimeToNanos(now - segmentStart_)) / 1000.0;
}

void
RcpClock::setSource(Source source)
{
	std::lock_guard<std::mutex> lock(mu_);
	if (source_ == source)
		return;
	freezeHostLocked();
	source_ = source;
	/* Switching source never auto-starts. Transport/engine must arm again. */
	armed_ = false;
}

RcpClock::Source
RcpClock::source() const
{
	std::lock_guard<std::mutex> lock(mu_);
	return source_;
}

void
RcpClock::resetStopped()
{
	std::lock_guard<std::mutex> lock(mu_);
	running_ = false;
	armed_ = false;
	musical_us_ = 0.0;
	segmentStart_ = 0;
}

void
RcpClock::resetAndRun()
{
	{
		std::lock_guard<std::mutex> lock(mu_);
		musical_us_ = 0.0;
		f8_rx_ = 0;
		f8_applied_ = 0;
		armed_ = true;
		if (source_ == Source::Ext) {
			/* Arm only: Continue/Start wait for the first F8 before advancing. */
			running_ = false;
			segmentStart_ = 0;
		} else {
			segmentStart_ = AudioGetCurrentHostTime();
			running_ = true;
		}
	}
	/* Outside lock: confirms this build is live and logging works. */
	dumpF8Stats(" START");
}

void
RcpClock::stop()
{
	std::lock_guard<std::mutex> lock(mu_);
	freezeHostLocked();
	armed_ = false;
}

void
RcpClock::resume()
{
	std::lock_guard<std::mutex> lock(mu_);
	armed_ = true;
	if (source_ == Source::Ext) {
		/* Keep musical_us_; do not advance until onTimingClock. */
		running_ = false;
		return;
	}
	if (running_)
		return;
	segmentStart_ = AudioGetCurrentHostTime();
	running_ = true;
}

double
RcpClock::elapsedUs()
{
	std::lock_guard<std::mutex> lock(mu_);
	double us = musical_us_;
	if (running_)
		us += hostDeltaUsLocked();
	return us;
}

void
RcpClock::onTimingClock()
{
	std::lock_guard<std::mutex> lock(mu_);
	/* Count every Timing Clock byte at the input, even if ignored. */
	f8_rx_++;
	if (source_ != Source::Ext || !armed_)
		return;
	if (qn_us_ <= 0)
		return;
	/* 24 MIDI clocks per quarter note. */
	musical_us_ += static_cast<double>(qn_us_) / 24.0;
	f8_applied_++;
}

uint64_t
RcpClock::f8Rx() const
{
	std::lock_guard<std::mutex> lock(mu_);
	return f8_rx_;
}

uint64_t
RcpClock::f8Applied() const
{
	std::lock_guard<std::mutex> lock(mu_);
	return f8_applied_;
}

void
RcpClock::dumpF8Stats(const char *tag) const
{
	std::lock_guard<std::mutex> lock(mu_);
	double us = musical_us_;
	if (running_)
		us += hostDeltaUsLocked();
	double approx_step = 0.0;
	if (qn_us_ > 0 && timebase_ > 0)
		approx_step = us * static_cast<double>(timebase_) / static_cast<double>(qn_us_);
	char line[512];
	std::snprintf(line, sizeof line,
		"[RcpClock]%s source=%s armed=%d running=%d "
		"f8_rx=%llu f8_applied=%llu musical_us=%.3f approx_step=%.3f "
		"qn_us=%d timebase=%d\n",
		tag ? tag : "",
		source_ == Source::Ext ? "EXT" : "INT",
		armed_ ? 1 : 0,
		running_ ? 1 : 0,
		(unsigned long long)f8_rx_,
		(unsigned long long)f8_applied_,
		us,
		approx_step,
		(int)qn_us_,
		timebase_);
	/* GUI .app often has no attached terminal; always mirror to a file. */
	std::fputs(line, stderr);
	std::fflush(stderr);
	FILE *fp = std::fopen("/tmp/sted_rcp_clock.log", "a");
	if (fp) {
		std::fputs(line, fp);
		std::fclose(fp);
	}
}

void
RcpClock::setTempo(double bpm, int timebase)
{
	std::lock_guard<std::mutex> lock(mu_);
	if (bpm > 0.0)
		qn_us_ = static_cast<int32_t>(60000000.0 / bpm + 0.5);
	if (timebase > 0)
		timebase_ = timebase;
}

void
RcpClock::setTempoUs(int32_t quarter_us, int timebase)
{
	std::lock_guard<std::mutex> lock(mu_);
	if (quarter_us > 0)
		qn_us_ = quarter_us;
	if (timebase > 0)
		timebase_ = timebase;
}

extern "C" {

void
rcp_sequence_clock_reset_and_run(void)
{
	gRcpClock.resetAndRun();
}

void
rcp_sequence_clock_stop(void)
{
	gRcpClock.stop();
}

double
rcp_sequence_clock_elapsed_us(void)
{
	return gRcpClock.elapsedUs();
}

void
rcp_clock_set_source(int ext)
{
	gRcpClock.setSource(ext ? RcpClock::Source::Ext : RcpClock::Source::Int);
}

int
rcp_clock_get_source(void)
{
	return gRcpClock.source() == RcpClock::Source::Ext ? 1 : 0;
}

void
rcp_clock_on_timing_clock(void)
{
	gRcpClock.onTimingClock();
}

void
rcp_clock_set_tempo(double bpm, int timebase)
{
	gRcpClock.setTempo(bpm, timebase);
}

void
rcp_clock_set_tempo_us(int32_t quarter_us, int timebase)
{
	gRcpClock.setTempoUs(quarter_us, timebase);
}

void
rcp_clock_dump_f8_stats(const char *tag)
{
	gRcpClock.dumpF8Stats(tag);
}

uint64_t
rcp_clock_f8_rx(void)
{
	return gRcpClock.f8Rx();
}

uint64_t
rcp_clock_f8_applied(void)
{
	return gRcpClock.f8Applied();
}

}
