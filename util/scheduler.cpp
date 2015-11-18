/*
 * Copyright (c) 2015 Ahmed Samy  <f.fallen45@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "scheduler.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <chrono>

#include <memory>
#include <algorithm>

#include <queue>
#include <list>

#include <iostream>

// Externed in header
Scheduler g_sched;

struct SchedulerEvent {
	SchedulerEvent(const SchedulerCallback &cb, uint32_t ms, bool cont)
	{
		static uint32_t lastId = 0;
		m_eventId = lastId++;
		m_callback = cb;
		m_continous = cont;
		m_ms = ms;
		start();
	}

	void start()
	{
		m_expiry = std::chrono::system_clock::now()
			+ std::chrono::milliseconds(m_ms);
	}

	bool continous() const { return m_continous; }
	bool expired() const { return std::chrono::system_clock::now() >= m_expiry; }
	uint32_t id() const { return m_eventId; }
	SchedulerCallback callback() const { return m_callback; }
	operator bool() const { return !!m_callback && expired(); }

private:
	SchedulerCallback m_callback;
	std::chrono::time_point<std::chrono::system_clock> m_expiry;
	uint32_t m_eventId, m_ms;
	bool m_continous;
};

class SchedulerImpl {
public:
	std::list<SchedulerEvent> events;
	std::mutex mutex;

public:
	SchedulerImpl() {
		m_stopped = false;
		m_thread = std::thread(std::bind(&SchedulerImpl::thread, this));
		m_dispatcherThread = std::thread(std::bind(&SchedulerImpl::dispatcherThread, this));
	}

	~SchedulerImpl() {
		m_stopped = true;
		m_condition.notify_one();
		m_thread.join();
		m_dispatcherThread.join();
	}

	void notify_all() { m_condition.notify_all(); }

protected:
	void thread();
	void dispatcherThread();

private:
	std::queue<SchedulerCallback> m_queue;
	std::thread m_thread;
	std::thread m_dispatcherThread;
	std::condition_variable m_condition;
	std::atomic_bool m_stopped;
};

Scheduler::Scheduler()
	: i(new SchedulerImpl())
{
}

Scheduler::~Scheduler()
{
	delete i;
}

uint32_t Scheduler::addEvent(const SchedulerCallback &cb, uint32_t ms, bool continous)
{
	SchedulerEvent ev(cb, ms, continous);

	bool notify = i->events.empty();;
	i->mutex.lock();
	i->events.push_back(ev);
	i->mutex.unlock();
	if (notify)
		i->notify_all();

	return ev.id();
}

void Scheduler::stopEvent(uint32_t id)
{
	std::lock_guard<std::mutex> lock(i->mutex);
	auto it = std::find_if(i->events.begin(), i->events.end(), [id] (const SchedulerEvent &e) { return e.id() == id; });
	if (it != i->events.end())
		i->events.erase(it);
}

void SchedulerImpl::dispatcherThread()
{
	std::unique_lock<std::mutex> m(mutex, std::defer_lock);

	while (!m_stopped) {
		bool cont = m_condition.wait_until(m, std::chrono::system_clock::now()
				+ std::chrono::milliseconds(10), [this] () { return !m_queue.empty(); } );
		if (m_stopped)
			break;
		else if (!cont)
			continue;

		while (!m_queue.empty()) {
			SchedulerCallback ec = m_queue.front();
			m_queue.pop();

			ec();
		}
	}
}

void SchedulerImpl::thread()
{
	std::unique_lock<std::mutex> m(mutex, std::defer_lock);

	while (!m_stopped) {
		m.lock();
		if (events.empty()) {
			// Let's wait there are events waiting
			m.unlock();
			m_condition.wait(m);
			if (m_stopped)
				break;
		}

		for (auto it = events.begin(); it != events.end();) {
			SchedulerEvent &e = *it;
			if (e) {
				// unlock, notify dispatcher and remove if so
				m_queue.push(e.callback());
				m.unlock();
				m_condition.notify_one();

				m.lock();
				if (e.continous()) {
					e.start();
					++it;
				} else {
					// purge
					it = events.erase(it);
				}
			} else {
				// just increment
				++it;
			}
		}

		m.unlock();
	}
}

