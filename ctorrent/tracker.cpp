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
#include "tracker.h"
#include "torrent.h"

#include <boost/array.hpp>

bool Tracker::query(const TrackerQuery &req)
{
	if (m_prot == "http")
		return httpRequest(req);

	return udpRequest(req);
}

bool Tracker::httpRequest(const TrackerQuery &r)
{
	asio::ip::tcp::resolver resolver(g_service);
	asio::ip::tcp::resolver::query query(m_host, m_port);
	boost::system::error_code error;

	asio::ip::tcp::resolver::iterator endpoint = resolver.resolve(query, error);
	asio::ip::tcp::resolver::iterator end;
	if (error) {
		m_torrent->handleTrackerError(this, "Unable to resolve host: " + error.message());
		return false;
	}

	asio::ip::tcp::socket socket(g_service);
	do {
		socket.close();
		socket.connect(*endpoint++, error);
	} while (error && endpoint != end);
	if (error) {
		m_torrent->handleTrackerError(this, "Failed to connect: " + error.message());
		return false;
	}

	std::string req;
	switch (r.event) {
	case TrackerEvent::None:	req = ""; break;			// prevent a useless warning
	case TrackerEvent::Completed: 	req = "event=completed&"; break;
	case TrackerEvent::Stopped:	req = "event=stopped&"; break;
	case TrackerEvent::Started:	req = "event=started&"; break;
	}

	// Used to extract some things.
	const uint8_t *handshake = m_torrent->handshake();
	char infoHash[20];
	memcpy(infoHash, &handshake[28], 20);

	asio::streambuf params;
	std::ostream buf(&params);
	buf << "GET /announce?" << req << "info_hash=" << urlencode(std::string(infoHash, 20)) << "&port=" << m_tport << "&compact=1&key=1337T0RRENT"
		<< "&peer_id=" << urlencode(std::string((const char *)m_torrent->peerId(), 20)) << "&downloaded=" << r.downloaded << "&uploaded=" << r.uploaded
		<< "&left=" << r.remaining << " HTTP/1.0\r\n"
		<< "Host: " << m_host << "\r\n"
		<< "\r\n";
	asio::write(socket, params);

	asio::streambuf response;
	try {
		asio::read_until(socket, response, "\r\n");
	} catch (const std::exception &e) {
		m_torrent->handleTrackerError(this, "Unable to read data: " + std::string(e.what()));
		return false;
	}

	std::istream rbuf(&response);
	std::string httpVersion;
	rbuf >> httpVersion;

	if (httpVersion.substr(0, 5) != "HTTP/") {
		m_torrent->handleTrackerError(this, "Tracker send an invalid HTTP version response");
		return false;
	}

	int status;
	rbuf >> status;
	if (status != 200) {
		std::ostringstream os;
		os << "Tracker failed to process our request: " << status;
		m_torrent->handleTrackerError(this, os.str());
		return false;
	}

	try {
		asio::read_until(socket, response, "\r\n\r\n");
	} catch (const std::exception &e) {
		m_torrent->handleTrackerError(this, "Unable to read data: " + std::string(e.what()));
		return false;
	}
	socket.close();

	// Seek to start of body
	std::string header;
	while (std::getline(rbuf, header) && header != "\r");
	if (!rbuf) {
		m_torrent->handleTrackerError(this, "Unable to get to tracker response body.");
		return false;
	}

	std::ostringstream os;
	os << &response;

	Bencode bencode;
	Dictionary dict = bencode.decode(os.str().c_str(), os.str().length());
	if (dict.empty()) {
		m_torrent->handleTrackerError(this, "Unable to decode tracker response body");
		return false;
	}

	if (dict.count("failure reason") != 0) {
		m_torrent->handleTrackerError(this, Bencode::cast<std::string>(dict["failure reason"]));
		return false;
	}

	m_timeToNextRequest = std::chrono::system_clock::now()
				+ std::chrono::milliseconds(Bencode::cast<int64_t>(dict["interval"]));
	m_torrent->connectToPeers(dict["peers"]);
	return true;
}

bool Tracker::udpRequest(const TrackerQuery &r)
{
	asio::ip::udp::resolver resolver(g_service);
	asio::ip::udp::resolver::query query(m_host, m_port);
	boost::system::error_code error;

	asio::ip::udp::resolver::iterator ep_iter = resolver.resolve(query, error);
	if (error) {
		m_torrent->handleTrackerError(this, "Unable to resolve host: " + error.message());
		return false;
	}

	asio::ip::udp::endpoint r_endpoint;
	asio::ip::udp::endpoint endpoint = *ep_iter;
	asio::ip::udp::socket socket(g_service);
	socket.open(endpoint.protocol());

	static std::random_device rd;
	static std::mt19937 generator(rd());
	static std::uniform_int_distribution<uint32_t> random(0x00, 0xFFFFFFFF);
	uint32_t tx = random(generator);

	uint8_t buf[16];
	writeBE64(&buf[0], 0x41727101980);	// connection id
	writeBE32(&buf[8], 0x00);		// action
	writeBE32(&buf[12], tx);

	if (socket.send_to(asio::buffer(&buf[0], 16), endpoint) != 16) {
		m_torrent->handleTrackerError(this, "Failed to initiate UDP transaction");
		socket.close();
		return false;
	}

	size_t len = socket.receive_from(asio::buffer(buf, 16), r_endpoint);
	if (len != 16) {
		m_torrent->handleTrackerError(this, "failed to receive data");
		socket.close();
		return false;
	}

	if (readBE32(&buf[0]) != 0) {
		m_torrent->handleTrackerError(this, "action mismatch");
		socket.close();
		return false;
	}

	if (readBE32(&buf[4]) != tx) {
		m_torrent->handleTrackerError(this, "transaction id mismatch");
		socket.close();
		return false;
	}

	uint64_t cid = readBE64(&buf[8]);
	uint8_t re[98];
	writeBE64(&re[0], cid);
	writeBE32(&re[8], 1);
	writeBE32(&re[12], tx);

	const uint8_t *handshake = m_torrent->handshake();
	memcpy(&re[16], &handshake[28], 20);
	memcpy(&re[36], m_torrent->peerId(), 20);

	writeBE64(&re[56], r.downloaded);
	writeBE64(&re[64], r.remaining);
	writeBE64(&re[72], r.uploaded);
	writeBE32(&re[80], (uint32_t)r.event);
	writeBE32(&re[84], 0);	// ip
	writeBE32(&re[88], 0);	// key
	writeBE32(&re[92], -1);	// num want
	writeBE16(&re[96], m_tport);

	if (socket.send_to(asio::buffer(&re[0], sizeof(re)), endpoint) != sizeof(re)) {
		m_torrent->handleTrackerError(this, "failed to send announce data");
		socket.close();
		return false;
	}

	uint8_t response[1500];
	len = socket.receive_from(asio::buffer(response, sizeof(response)), r_endpoint);
	socket.close();
	if (len < 20) {
		m_torrent->handleTrackerError(this, "expected at least 20 bytes response");
		return false;
	}

	if (readBE32(&response[0]) != 1) {
		m_torrent->handleTrackerError(this, "2: action mismatch");
		return false;
	}

	if (readBE32(&response[4]) != tx) {
		m_torrent->handleTrackerError(this, "2: transaction mismatch");
		return false;
	}

	m_timeToNextRequest = std::chrono::system_clock::now() + std::chrono::milliseconds(readBE32(&response[8]));
	m_torrent->rawConnectPeers(&response[20], len - 20);
	return true;
}

