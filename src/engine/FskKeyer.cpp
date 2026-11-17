#include "FskKeyer.h"

#include <chrono>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <mmsystem.h>
#else
#  include <pthread.h>
#  include <sched.h>
#endif

namespace {
constexpr int64_t kSpinNs = 2000000;		// busy-wait for the last 2 ms before an edge
}

int64_t FskKeyer::nowNs()
{
	using namespace std::chrono;
	return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

void FskKeyer::start(Sink sink)
{
	stop();
	{
		std::lock_guard<std::mutex> l(m_sinkMx);
		m_sink = std::move(sink);
	}
	{
		std::lock_guard<std::mutex> l(m_mx);
		m_quit = false;
		m_q.clear();
	}
	resetStats();
	m_thread = std::thread([this] { run(); });
}

void FskKeyer::stop()
{
	if( !m_thread.joinable() ) return;
	{
		std::lock_guard<std::mutex> l(m_mx);
		m_quit = true;
		m_q.clear();
	}
	m_cv.notify_all();
	m_thread.join();
}

void FskKeyer::push(const std::vector<Edge> &edges)
{
	if( edges.empty() ) return;
	{
		std::lock_guard<std::mutex> l(m_mx);
		m_q.insert(m_q.end(), edges.begin(), edges.end());
	}
	m_cv.notify_all();
}

size_t FskKeyer::pending() const
{
	std::lock_guard<std::mutex> l(m_mx);
	return m_q.size();
}

// gen == ~0: unconditional.  Otherwise the edge is dropped when abort() ran after it was popped.
void FskKeyer::fire(bool mark, uint64_t gen)
{
	std::lock_guard<std::mutex> l(m_sinkMx);
	if( gen != ~uint64_t(0) && gen != m_gen.load() ) return;
	if( m_sink ) m_sink(mark);
}

void FskKeyer::abort(bool mark)
{
	{
		std::lock_guard<std::mutex> l(m_mx);
		m_q.clear();
		m_gen++;
	}
	m_cv.notify_all();
	fire(mark, ~uint64_t(0));
}

void FskKeyer::run()
{
#ifdef _WIN32
	timeBeginPeriod(1);
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#else
	sched_param sp{};
	sp.sched_priority = 10;
	pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);		// needs privileges; failure is fine
#endif
	std::unique_lock<std::mutex> lk(m_mx);
	while( !m_quit ){
		if( m_q.empty() ){
			m_cv.wait(lk);
			continue;
		}
		const Edge e = m_q.front();
		const int64_t dt = e.ns - nowNs();
		if( dt > kSpinNs ){
			m_cv.wait_for(lk, std::chrono::nanoseconds(dt - kSpinNs));	// re-evaluate afterwards
			continue;
		}
		if( dt > 0 ){
			lk.unlock();
			while( nowNs() < e.ns ) std::this_thread::yield();
			lk.lock();
			if( m_quit ) break;
			// the queue may have been aborted while spinning
			if( m_q.empty() || m_q.front().ns != e.ns || m_q.front().mark != e.mark ) continue;
		}
		m_q.pop_front();
		const uint64_t gen = m_gen.load();
		lk.unlock();
		const int64_t late = nowNs() - e.ns;
		int64_t prev = m_maxLate.load();
		while( late > prev && !m_maxLate.compare_exchange_weak(prev, late) ) {}
		fire(e.mark, gen);
		m_fired++;
		lk.lock();
	}
#ifdef _WIN32
	timeEndPeriod(1);
#endif
}
