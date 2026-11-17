// FskKeyer: plays a list of timed mark/space edges on a control line (DTR or RTS) with
// sub-millisecond accuracy.
//
// A dedicated thread sleeps until shortly before each edge and then busy-waits on a monotonic
// clock for the last ~2 ms, so the accuracy does not depend on the OS timer granularity.  The
// thread asks for elevated scheduling priority (best effort, ignored if not permitted).
#ifndef FSKKEYER_H
#define FSKKEYER_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class FskKeyer
{
public:
	struct Edge {
		int64_t ns;			// absolute time on the nowNs() clock
		bool    mark;		// new line state
	};
	using Sink = std::function<void(bool mark)>;

	FskKeyer() = default;
	~FskKeyer() { stop(); }
	FskKeyer(const FskKeyer &) = delete;
	FskKeyer &operator=(const FskKeyer &) = delete;

	static int64_t nowNs();							// monotonic clock shared with the scheduler

	void start(Sink sink);
	void stop();									// joins the thread, drops pending edges
	void push(const std::vector<Edge> &edges);		// edges must be in time order
	void abort(bool mark);							// drop pending edges and drive the line to 'mark' now
	size_t pending() const;

	// statistics (for tests / diagnostics)
	int64_t maxLatenessNs() const { return m_maxLate.load(); }
	int64_t firedCount() const { return m_fired.load(); }
	void resetStats() { m_maxLate = 0; m_fired = 0; }

private:
	void run();
	void fire(bool mark, uint64_t gen);

	mutable std::mutex m_mx;			// guards m_q, m_quit
	std::condition_variable m_cv;
	std::deque<Edge> m_q;
	std::mutex m_sinkMx;				// serialises sink calls (thread vs. abort())
	Sink m_sink;
	std::thread m_thread;
	bool m_quit = false;
	std::atomic<int64_t> m_maxLate{0};
	std::atomic<int64_t> m_fired{0};
	std::atomic<uint64_t> m_gen{0};		// incremented by abort(): edges popped before it are dropped
};

#endif
