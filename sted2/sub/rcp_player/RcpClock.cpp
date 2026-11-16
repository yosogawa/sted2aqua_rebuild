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
	, f8_q_head_(0)
	, f8_q_tail_(0)
	, f8_q_count_(0)
	, ext_acc_(0)
	, ext_owed_(0)
	, ext_slot_index_(0)
	, ext_ticks_(0)
	, ext_origin_us_(0.0)
	, ext_slot_host_(0)
	, ext_f8_host_(0)
	, ext_last_f8_host_(0)
	, ext_have_last_f8_(false)
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
RcpClock::clearExtScheduleLocked(bool resetAcc)
{
	f8_q_head_ = 0;
	f8_q_tail_ = 0;
	f8_q_count_ = 0;
	ext_owed_ = 0;
	ext_slot_index_ = 0;
	ext_slot_host_ = 0;
	ext_f8_host_ = 0;
	ext_last_f8_host_ = 0;
	ext_have_last_f8_ = false;
	if (resetAcc)
		ext_acc_ = 0;
}

int
RcpClock::f8TickCountLocked()
{
	ext_acc_ += timebase_;
	int n = ext_acc_ / 24;
	ext_acc_ %= 24;
	return n;
}

uint64_t
RcpClock::tempoF8HostLocked() const
{
	double periodUs = static_cast<double>(qn_us_) / 24.0;
	if (periodUs < 1.0)
		periodUs = 1.0;
	return AudioConvertNanosToHostTime(static_cast<uint64_t>(periodUs * 1000.0));
}

void
RcpClock::advanceOneTickLocked()
{
	/* Same product as rcpplayer_step_to_microseconds, so a tick lands on
	   the step instead of a fraction short of it. */
	ext_ticks_++;
	musical_us_ = ext_origin_us_ + static_cast<double>(ext_ticks_)
		/ static_cast<double>(timebase_) * static_cast<double>(qn_us_);
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
	clearExtScheduleLocked(true);
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
	ext_ticks_ = 0;
	ext_origin_us_ = 0.0;
	clearExtScheduleLocked(true);
}

void
RcpClock::resetAndRun()
{
	{
		std::lock_guard<std::mutex> lock(mu_);
		musical_us_ = 0.0;
		f8_rx_ = 0;
		f8_applied_ = 0;
		ext_ticks_ = 0;
		ext_origin_us_ = 0.0;
		clearExtScheduleLocked(true);
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
	/* MIDI_CLOCK_DEBUG only. Normal builds do not write the log. */
	dumpF8Stats(" START");
}

void
RcpClock::stop()
{
	std::lock_guard<std::mutex> lock(mu_);
	freezeHostLocked();
	armed_ = false;
	/* Drop the unfinished clock. A pause gap must not become the next period. */
	clearExtScheduleLocked(false);
}

void
RcpClock::resume()
{
	std::lock_guard<std::mutex> lock(mu_);
	armed_ = true;
	if (source_ == Source::Ext) {
		/* Keep musical_us_. The next taken tick is tick 1 of a new F8. */
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
RcpClock::setElapsedUs(double us)
{
	std::lock_guard<std::mutex> lock(mu_);
	freezeHostLocked();
	if (us < 0.0)
		us = 0.0;
	musical_us_ = us;
	ext_origin_us_ = us;
	ext_ticks_ = 0;
	/* INT keeps a fresh segment base if still armed so the next host
	   delta starts from this position. EXT stays frozen until F8. */
	if (armed_ && source_ == Source::Int) {
		segmentStart_ = AudioGetCurrentHostTime();
		running_ = true;
	}
}

void
RcpClock::onTimingClock()
{
	std::lock_guard<std::mutex> lock(mu_);
	/* Count every Timing Clock byte at the input, even if ignored. */
	f8_rx_++;
	if (source_ != Source::Ext || !armed_)
		return;
	if (qn_us_ <= 0 || timebase_ <= 0)
		return;
	if (f8_q_count_ >= kExtF8Queue)
		return;
	f8_host_[f8_q_head_] = AudioGetCurrentHostTime();
	f8_q_head_ = (f8_q_head_ + 1) % kExtF8Queue;
	f8_q_count_++;
}

bool
RcpClock::takeExtTick()
{
	std::lock_guard<std::mutex> lock(mu_);
	if (source_ != Source::Ext || !armed_ || qn_us_ <= 0 || timebase_ <= 0)
		return false;
	uint64_t now = AudioGetCurrentHostTime();
	if (ext_owed_ > 0) {
		if (f8_q_count_ == 0) {
			uint64_t due = ext_f8_host_ + ext_slot_host_ * static_cast<uint64_t>(ext_slot_index_);
			if (now < due)
				return false;
		}
		advanceOneTickLocked();
		ext_owed_--;
		ext_slot_index_++;
		return true;
	}
	if (f8_q_count_ == 0)
		return false;
	uint64_t host = f8_host_[f8_q_tail_];
	f8_q_tail_ = (f8_q_tail_ + 1) % kExtF8Queue;
	f8_q_count_--;
	int n = f8TickCountLocked();
	uint64_t period;
	if (!ext_have_last_f8_ || host <= ext_last_f8_host_)
		period = tempoF8HostLocked();
	else
		period = host - ext_last_f8_host_;
	ext_have_last_f8_ = true;
	ext_last_f8_host_ = host;
	ext_f8_host_ = host;
	f8_applied_++;
	if (n < 1)
		return false;
	ext_slot_host_ = period / static_cast<uint64_t>(n);
	if (ext_slot_host_ == 0)
		ext_slot_host_ = 1;
	ext_owed_ = n - 1;
	ext_slot_index_ = 1;
	advanceOneTickLocked();
	return true;
}

int
RcpClock::extWaitUs() const
{
	std::lock_guard<std::mutex> lock(mu_);
	if (source_ != Source::Ext || !armed_ || qn_us_ <= 0 || timebase_ <= 0)
		return 5000;
	if (ext_owed_ > 0) {
		if (f8_q_count_ > 0)
			return 0;
		uint64_t due = ext_f8_host_ + ext_slot_host_ * static_cast<uint64_t>(ext_slot_index_);
		uint64_t now = AudioGetCurrentHostTime();
		if (now >= due)
			return 0;
		double us = static_cast<double>(AudioConvertHostTimeToNanos(due - now)) / 1000.0;
		if (us < 1.0)
			return 0;
		if (us > 1000.0)
			return 1000;
		return static_cast<int>(us);
	}
	if (f8_q_count_ > 0)
		return 0;
	return 5000;
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
#ifdef MIDI_CLOCK_DEBUG
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
	/* GUI .app often has no attached terminal; mirror the same line to a file. */
	std::fputs(line, stderr);
	std::fflush(stderr);
	FILE *fp = std::fopen("/tmp/sted_rcp_clock.log", "a");
	if (fp) {
		std::fputs(line, fp);
		std::fclose(fp);
	}
#else
	(void)tag;
#endif
}

void
RcpClock::setTempo(double bpm, int timebase)
{
	std::lock_guard<std::mutex> lock(mu_);
	if (bpm > 0.0)
		qn_us_ = static_cast<int32_t>(60000000.0 / bpm + 0.5);
	if (timebase > 0)
		timebase_ = timebase;
	ext_origin_us_ = musical_us_;
	ext_ticks_ = 0;
}

void
RcpClock::setTempoUs(int32_t quarter_us, int timebase)
{
	std::lock_guard<std::mutex> lock(mu_);
	if (quarter_us > 0)
		qn_us_ = quarter_us;
	if (timebase > 0)
		timebase_ = timebase;
	ext_origin_us_ = musical_us_;
	ext_ticks_ = 0;
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
rcp_sequence_clock_set_elapsed_us(double us)
{
	gRcpClock.setElapsedUs(us);
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

int
rcp_clock_take_ext_tick(void)
{
	return gRcpClock.takeExtTick() ? 1 : 0;
}

int
rcp_clock_ext_wait_us(void)
{
	return gRcpClock.extWaitUs();
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
