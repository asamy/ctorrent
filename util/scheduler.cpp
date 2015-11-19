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
		static uint32_t lastId = 0;
		m_eventId = lastId++;
		m_callback = cb;
		m_expiry = std::chrono::system_clock::now()
			+ std::chrono::milliseconds(ms);

	}
	~SchedulerEvent() { m_callback = nullptr; }

	bool expired() const { return std::chrono::system_clock::now() >= m_expiry; }
	uint32_t id() const { return m_eventId; }
	SchedulerCallback callback() const { return m_callback; }
	std::chrono::time_point<std::chrono::system_clock> expiry() const { return m_expiry; }
	operator bool() const { return !!m_callback && expired(); }


private:
	SchedulerCallback m_callback;
	std::chrono::time_point<std::chrono::system_clock> m_expiry;
	uint32_t m_eventId;
};

struct SchedulerLess {
	bool operator() (const SchedulerEvent &lhs, const SchedulerEvent &rhs) const {
		return lhs.expiry() < rhs.expiry();
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

uint32_t Scheduler::addEvent(const SchedulerCallback &cb, uint32_t ms)
{
	SchedulerEvent ev(cb, ms);

	bool notify = i->events.empty();;
	i->mutex.lock();
	i->events.push(ev);
	i->mutex.unlock();
	if (notify)
		i->notify_all();

	return ev.id();
}

void Scheduler::stopEvent(uint32_t id)
{
	std::lock_guard<std::mutex> lock(i->mutex);
	i->pendingRemoval.insert(id);
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
		if (events.empty())
			m_condition.wait(m);
		else
			m_condition.wait_until(m, events.top().expiry());

		if (m_stopped)
			break;

		SchedulerEvent e = events.top();
		events.pop();

		auto it = pendingRemoval.find(e.id());
		if (it != pendingRemoval.end()) {
			pendingRemoval.erase(it);
			continue;
		}

		m_queue.push(e.callback());
		m_condition.notify_all();
	}
}

