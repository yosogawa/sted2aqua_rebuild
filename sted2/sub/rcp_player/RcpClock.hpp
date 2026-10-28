#ifndef RCP_CLOCK_HPP
#define RCP_CLOCK_HPP

#include <mutex>
#include <cstdint>

/*
  Musical time for one play session. Separate from RcpTransport phase.

  Transport only arms / freezes / resets this clock. Whether time advances
  is decided here:
    INT: host time while armed and running_
    EXT: MIDI Timing Clock (0xF8), 24 PPQN, only while armed_

  MIDI Timing Clock (0xF8) is not RCP loop-end 0xF8.
  MTC is out of scope.
*/
class RcpClock {
public:
	enum class Source {
		Int = 0, /* internal tempo / host-time accumulation */
		Ext = 1  /* external MIDI Timing Clock */
	};

	RcpClock();
	RcpClock(const RcpClock &) = delete;
	RcpClock &operator=(const RcpClock &) = delete;

	void setSource(Source source);
	Source source() const;

	/* Same call sites as the former SequenceClock. */
	void resetStopped();
	void resetAndRun();
	void stop();   /* freeze: keep elapsed, stop advancing */
	void resume(); /* INT: start host accumulation; EXT: arm only (wait for F8) */
	double elapsedUs();
	/* Set frozen musical position. Freezes INT host accumulation first.
	   Does not change armed_/source_. Used by SPP Locate and engine entry
	   after Start that follows an SPP. */
	void setElapsedUs(double us);

	/* Thin aliases for Transport wording in the design notes. */
	void reset() { resetStopped(); }
	void freeze() { stop(); }
	void arm() { resume(); }

	/* EXT: one MIDI Timing Clock tick (24 PPQN). Always counts RX;
	   advances musical time only when Ext + armed. */
	void onTimingClock();

	/* Tempo used for EXT F8 → µs (qn_us_ = MIDI meta tempo). INT ignores. */
	void setTempo(double bpm, int timebase);
	void setTempoUs(int32_t quarter_us, int timebase);

	/* Diagnostic: F8 RX / applied since last resetAndRun. */
	uint64_t f8Rx() const;
	uint64_t f8Applied() const;
	void dumpF8Stats(const char *tag) const;

private:
	void freezeHostLocked(); /* must hold mu_ */
	double hostDeltaUsLocked() const; /* must hold mu_ */

	mutable std::mutex mu_;
	Source source_;
	bool running_; /* INT: accumulating host time */
	bool armed_;   /* Transport wants time to run; EXT waits for F8 until then */
	double musical_us_; /* frozen musical position (both sources) */
	uint64_t segmentStart_; /* INT host segment start */
	int32_t qn_us_; /* µs per quarter note (default 500000 = 120 BPM) */
	int timebase_;
	uint64_t f8_rx_;      /* every 0xF8 at MIDI IN */
	uint64_t f8_applied_; /* F8 that advanced musical_us_ */
};

extern RcpClock gRcpClock;

#endif
