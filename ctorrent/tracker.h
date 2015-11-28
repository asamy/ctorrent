/*
 * Copyright (c) 2014-2015 Ahmed Samy  <f.fallen45@gmail.com>
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
#ifndef __TRACKER_H
#define __TRACKER_H

#include <string>
#include <chrono>
#include <memory>

typedef std::chrono::time_point<std::chrono::system_clock> TimePoint;

enum class TrackerEvent {
	None = 0,
	Completed = 1,
	Started = 2,
	Stopped = 3,
};

struct TrackerQuery {
	TrackerEvent event;
	size_t downloaded;
	size_t uploaded;
	size_t remaining;
};

class Torrent;
class Tracker
{
	enum TrackerType {
		TrackerHTTP,
		TrackerUDP,
	};

public:
	Tracker(Torrent *torrent, const std::string &host, const std::string &port, const std::string &proto, uint16_t tport)
		: m_torrent(torrent),
		  m_tport(tport),
		  m_host(host),
		  m_port(port)
	{
		m_type = proto == "http" ? TrackerHTTP : TrackerUDP;
	}

	std::string host() const { return m_host; }
	std::string port() const { return m_port; }

	// Start querying this tracker for peers etc.
	bool query(const TrackerQuery &request);
	bool timeUp(void) { return std::chrono::system_clock::now() >= m_timeToNextRequest; }
	void setNextRequestTime(const TimePoint &p) { m_timeToNextRequest = p; }

protected:
	bool httpRequest(const TrackerQuery &r);
	bool udpRequest(const TrackerQuery &r);

private:
	Torrent *m_torrent;
	TimePoint m_timeToNextRequest;

	TrackerType m_type;
	uint16_t m_tport;
	std::string m_host;
	std::string m_port;

	friend class Torrent;
};

#endif

