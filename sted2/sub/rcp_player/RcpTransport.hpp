#ifndef RCP_TRANSPORT_HPP
#define RCP_TRANSPORT_HPP

#include <atomic>

/* One transport for the process. Constructed at load, not in service.
   setValid() enters service. clearValid() leaves it.
   The constructor records Stopped and does not write rcd or start a thread.
   start, continuePlay, stop, search, and fullStop refuse while the gate is closed.
   While it is open they record the phase and write sts.
   fullStop records Stopped. The caller invokes it after the player thread
   has exited, so a live Held thread does not observe it.
   attach() records the initial Stopped result once rcd exists. It does not
   open the gate. setActive() records whether the player thread is performing
   and writes act. Callers read that through active(), not through rcd. */
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
	Phase phase() const;

private:
	bool inService() const;
	void setPhase(Phase phase);

	std::atomic<bool> valid_;
	std::atomic<int> phase_;
	std::atomic<int> active_;
};

#endif
