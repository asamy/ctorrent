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
#include <unordered_set>

#include <iostream>

// Externed in header
Scheduler g_sched;

struct SchedulerEvent {
	SchedulerEvent(const SchedulerCallback &cb, uint32_t ms)
	{
		static uint32_t lastId = 1;
		m_eventId = lastId++;
		m_callback = cb;
		m_expiry = std::chrono::system_clock::now()
			+ std::chrono::milliseconds(ms);
	}
	~SchedulerEvent() { m_callback = nullptr; }

	uint32_t id() const { return m_eventId; }
	SchedulerCallback callback() const { return m_callback; }
	bool expired() const { return std::chrono::system_clock::now() >= m_expiry; }
	std::chrono::time_point<std::chrono::system_clock> expiry() const { return m_expiry; }

private:
	SchedulerCallback m_callback;
	std::chrono::time_point<std::chrono::system_clock> m_expiry;
	uint32_t m_eventId;
};

struct SchedulerLess {
	bool operator() (const SchedulerEvent &lhs, const SchedulerEvent &rhs) const {
		return lhs.expiry() > rhs.expiry();
	}
};

class SchedulerImpl {
public:
	std::priority_queue<SchedulerEvent, std::deque<SchedulerEvent>, SchedulerLess> events;
	std::unordered_set<uint32_t> pendingRemoval;
	std::mutex mutex;

public:
	SchedulerImpl() {
		m_stopped = false;
		m_thread = std::thread(std::bind(&SchedulerImpl::thread, this));
		m_dispatcherThread = std::thread(std::bind(&SchedulerImpl::dispatcherThread, this));
	}

	~SchedulerImpl() {
		stop();
	}

	void stop() {
		mutex.lock();
		m_stopped = true;
		mutex.unlock();
		m_condition.notify_all();
		m_thread.join();
		m_dispatcherThread.join();
	}
	void notify_one() { m_condition.notify_one(); }
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

uint32_t Scheduler::addEvent(const SchedulerCallback &cb, uint32_t ms)
{
	SchedulerEvent ev(cb, ms);

	bool notify = i->events.empty();
	i->mutex.lock();
	i->events.push(ev);
	i->mutex.unlock();
	if (notify)
		i->notify_one();

	return ev.id();
}

void Scheduler::stopEvent(uint32_t id)
{
	std::lock_guard<std::mutex> lock(i->mutex);
	i->pendingRemoval.insert(id);
}

void Scheduler::stop()
{
	delete i;
	i = nullptr;
}

void SchedulerImpl::dispatcherThread()
{
	std::unique_lock<std::mutex> m(mutex, std::defer_lock);

	while (!m_stopped) {
		m.lock();
		m_condition.wait(m, [this] () { return !m_queue.empty() || m_stopped; } );
		if (m_stopped)
			break;

		while (!m_queue.empty()) {
			SchedulerCallback ec = m_queue.front();
			m_queue.pop();

			ec();
		}
		m.unlock();
	}
}

void SchedulerImpl::thread()
{
	std::unique_lock<std::mutex> m(mutex, std::defer_lock);

	while (!m_stopped) {
		m.lock();
		if (events.empty()) {
			m_condition.wait(m);
			if (m_stopped)
				break;
		}

		// Ugly hack for newly added events
fart:
		bool cont = m_condition.wait_until(m, std::chrono::system_clock::now()
				+ std::chrono::milliseconds(5), [this] () { return events.top().expired() || m_stopped; });
		if (m_stopped)
			break;
		else if (!cont)
			goto fart;

		SchedulerEvent e = events.top();
		events.pop();

		auto it = pendingRemoval.find(e.id());
		if (it != pendingRemoval.end()) {
			pendingRemoval.erase(it);
			m.unlock();
			continue;
		}

		m_queue.push(e.callback());
		m.unlock();
		m_condition.notify_all();
	}
}

