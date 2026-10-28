#ifndef RCP_TRANSPORT_HPP
#define RCP_TRANSPORT_HPP

#include <atomic>
#include <cstdint>

/* One transport for the process. Constructed at load, not in service.
   setValid() enters service. clearValid() leaves it.
   The constructor records Stopped and does not write rcd or start a thread.
   start, continuePlay, stop, search, and fullStop refuse while the gate is closed.
   While it is open they record the phase and write sts.
   fullStop records Stopped. The caller invokes it after the player thread
   has exited, so a live Held thread does not observe it.
   attach() records the initial Stopped result once rcd exists. It does not
   open the gate. setActive() records whether the player thread is performing
   and writes act. Callers read that through active(), not through rcd.
   start() prepares the player from rcd when a playable image exists
   (measure table, rcpplayer_init; stops a live engine if any), then
   records Running and resets RcpClock stopped. Prep is skipped when
   data is missing; stopping is skipped when no engine is live. The
   phase change still runs after a skipped prep. continuePlay() and
   search() arm/resume the clock. stop() and fullStop() freeze it.
   Musical time lives in RcpClock, not in this class. */
class RcpTransport {
public:
	enum class Phase {
		Stopped = 0,
		Running = 1,
		Held = 2,
		Search = 3
	};

	RcpTransport();
	RcpTransport(const RcpTransport &) = delete;
	RcpTransport &operator=(const RcpTransport &) = delete;

	void setValid();
	void clearValid();
	void attach();
	void setActive(int on);
	int active() const;
	void start();
	void continuePlay();
	void stop();
	void fullStop();
	void search(int track, int bar);
	/* MIDI SPP (14-bit). Stopped/Held: set stepcount, clock, and player
	   read position. Does not enter Search. Running/Search/recording: store
	   only. */
	void songPosition(uint16_t spp);
	uint16_t lastSongPosition() const;
	/* Engine entry: consume the elapsed µs prepared by SPP / start(). */
	bool takeStartElapsedUs(double *us);
	Phase phase() const;

private:
	bool inService() const;
	void setPhase(Phase phase);
	void clearSongPositionLocate();
	void applySongPositionLocate(uint16_t spp);

	std::atomic<bool> valid_;
	std::atomic<int> phase_;
	std::atomic<int> active_;
	std::atomic<uint16_t> last_spp_;
	std::atomic<bool> spp_valid_;
	std::atomic<bool> start_elapsed_valid_;
	std::atomic<double> start_elapsed_us_;
};

#endif
