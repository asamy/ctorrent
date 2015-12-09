/*
 * Copyright (c) 2014, 2015 Ahmed Samy  <f.fallen45@gmail.com>
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
#include "torrent.h"

#include <net/connection.h>

#include <util/auxiliar.h>
#include <util/scheduler.h>

#include <algorithm>
#include <iostream>
#include <thread>


Torrent::Torrent()
	: m_listener(nullptr),
	  m_fileManager(this),
	  m_uploadedBytes(0),
	  m_downloadedBytes(0),
	  m_wastedBytes(0),
	  m_hashMisses(0)
{

}

Torrent::~Torrent()
{
	for (Tracker *tracker : m_activeTrackers)
		delete tracker;
	m_activeTrackers.clear();
	m_peers.clear();

	if (m_listener)
		m_listener->stop();
	delete m_listener;
}

bool Torrent::open(const std::string &fileName, const std::string &downloadDir)
{
	if (!m_meta.parse(fileName))
		return false;

	const uint8_t *checkSum = m_meta.checkSum();
	m_handshake[0] = 0x13;					// 19 length of string "BitTorrent protocol"
	memcpy(&m_handshake[1], "BitTorrent protocol", 19);
	memset(&m_handshake[20], 0x00, 8);			// reserved bytes (last |= 0x01 for DHT or last |= 0x04 for FPE)
	memcpy(&m_handshake[28], checkSum, 20);			// info hash
	memcpy(&m_handshake[48], "-CT11000", 8);		// Azureus-style peer id (-CT0000-XXXXXXXXXXXX)
	memcpy(&m_peerId[0], "-CT11000", 8);

	static std::random_device rd;
	static std::knuth_b generator(rd());
	static std::uniform_int_distribution<uint8_t> random(0x00, 0xFF);
	for (size_t i = 8; i < 20; ++i)
		m_handshake[56 + i] = m_peerId[i] = random(generator);

	std::string dir = downloadDir;
	if (!ends_with(dir, PATH_SEP))
		dir += PATH_SEP;

	return m_fileManager.registerFiles(dir, m_meta.files());
}

double Torrent::eta()
{
	clock_t elapsed_s = elapsed() / CLOCKS_PER_SEC;
	size_t downloaded = computeDownloaded();
	return (double)(m_meta.totalSize() - downloaded) * elapsed_s / (double)downloaded;
}

double Torrent::downloadSpeed()
{
	clock_t elapsed_s = elapsed() / CLOCKS_PER_SEC;
	size_t downloaded = computeDownloaded();
	if (elapsed_s == 0)
		return 0.0f;

	double speed = (double)(downloaded / elapsed_s);
	return (speed / 1024) * 0.0125;
}

clock_t Torrent::elapsed()
{
	return clock() - m_startTime;
}

TrackerQuery Torrent::makeTrackerQuery(TrackerEvent event)
{
	size_t downloaded = computeDownloaded();
	TrackerQuery q = {
		.event = event,
		.downloaded = downloaded,
		.uploaded = m_uploadedBytes,
		.remaining = m_meta.totalSize() - downloaded
	};

	return q;
}

Torrent::DownloadState Torrent::download(uint16_t port)
{
	if (isFinished())
		return DownloadState::AlreadyDownloaded;

	if (!queryTrackers(makeTrackerQuery(TrackerEvent::Started), port))
		return DownloadState::TrackerQueryFailure;

	m_startTime = clock();
	while (!isFinished()) {
		for (Tracker *tracker : m_activeTrackers)
			if (tracker->timeUp())
				tracker->query(makeTrackerQuery(TrackerEvent::None));
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	TrackerEvent event = isFinished() ? TrackerEvent::Completed : TrackerEvent::Stopped;
	TrackerQuery q = makeTrackerQuery(event);
	for (Tracker *tracker : m_activeTrackers) {
		tracker->query(q);
		delete tracker;
	}

	m_activeTrackers.clear();
	return event == TrackerEvent::Completed ? DownloadState::Completed : DownloadState::NetworkError;
}

bool Torrent::seed(uint16_t port)
{
	if (!m_listener) {
		m_listener = new(std::nothrow) Server(port);
		if (!m_listener)
			return false;
	}

	if (!queryTrackers(makeTrackerQuery(TrackerEvent::Started), port))
		return false;

	// Just loop and wait for new connections
	while (!m_listener->stopped()) {
		m_listener->accept(
			[this] (const ConnectionPtr &c) {
				auto peer = std::make_shared<Peer>(c, this);
				peer->verify();
			}
		);
	}

	TrackerQuery q = makeTrackerQuery(TrackerEvent::Stopped);
	for (Tracker *tracker : m_activeTrackers) {
		tracker->query(q);
		delete tracker;
	}

	m_activeTrackers.clear();
	return true;
}

bool Torrent::queryTrackers(const TrackerQuery &query, uint16_t port)
{
	bool success = queryTracker(m_meta.tracker(), query, port);
	if (m_meta.trackers().empty()) {
		if (!success)
			std::cerr << m_meta.name() << ": queryTracker(): This torrent does not provide multiple trackers" << std::endl;

		return success;
	}

	bool alt = false;
	for (const boost::any &s : m_meta.trackers()) {
		if (s.type() == typeid(VectorType)) {
			const VectorType &vType = Bencode::cast<VectorType>(&s);
			for (const boost::any &announce : vType)
				alt = queryTracker(Bencode::cast<std::string>(&announce), query, port);
		} else if (s.type() == typeid(std::string))
			alt = queryTracker(Bencode::cast<std::string>(&s), query, port);
		else
			std::cerr << m_meta.name() << ": warning: unkown tracker type: " << s.type().name() << std::endl;
	}

	return success || alt;
}

bool Torrent::queryTracker(const std::string &furl, const TrackerQuery &q, uint16_t tport)
{
	UrlData url = parseUrl(furl);
	std::string host = URL_HOSTNAME(url);
	if (host.empty()) {
		std::cerr << m_meta.name() << ": queryTracker(): failed to parse announce url: " << furl << std::endl;
		return false;
	}

	std::string port = URL_SERVNAME(url);
	std::string protocol = URL_PROTOCOL(url);

	Tracker *tracker = new Tracker(this, host, port, protocol, tport);
	if (!tracker->query(q))
		return false;

	m_activeTrackers.push_back(tracker);
	return true;
}

void Torrent::rawConnectPeers(const uint8_t *peers, size_t size)
{
	m_peers.reserve(size / 6);

	// 6 bytes each (first 4 is ip address, last 2 port) all in big endian notation
	for (size_t i = 0; i < size; i += 6) {
		const uint8_t *iport = peers + i;
		uint32_t ip = isLittleEndian() ? readLE32(iport) : readBE32(iport);
		if (ip == 0 || m_blacklisted.find(ip) != m_blacklisted.end())
			continue;

		auto it = m_peers.find(ip);
		if (it != m_peers.end())
			continue;

		// Asynchronously connect to that peer, and do not add it to our
		// active peers list unless a connection was established successfully.
		auto peer = std::make_shared<Peer>(this);
		peer->connect(ip2str(ip), std::to_string(readBE16(iport + 4)));
		m_blacklisted.insert(ip);  // remove upon connection
	}
}

void Torrent::rawConnectPeer(Dictionary &peerInfo)
{
	std::string peerId = boost::any_cast<std::string>(peerInfo["peer id"]);
	std::string ip = boost::any_cast<std::string>(peerInfo["ip"]);
	int64_t port = boost::any_cast<int64_t>(peerInfo["port"]);

	uint32_t strip = str2ip(ip);
	if (m_blacklisted.find(strip) != m_blacklisted.end()
		|| m_peers.find(strip) != m_peers.end())
		return;

	// Asynchronously connect to that peer, and do not add it to our
	// active peers list unless a connection was established successfully.
	auto peer = std::make_shared<Peer>(this);
	peer->setId(peerId);
	peer->connect(ip, std::to_string(port));
	m_blacklisted.insert(strip); // remove upon connection
}

void Torrent::connectToPeers(const boost::any &_peers)
{
	if (isFinished())
		return;

	if (_peers.type() == typeid(std::string)) {
		std::string peers = *boost::unsafe_any_cast<std::string>(&_peers);
		return rawConnectPeers((const uint8_t *)peers.c_str(), peers.length());
	}

	if (_peers.type() == typeid(Dictionary)) {	// no compat
		Dictionary peers = *boost::unsafe_any_cast<Dictionary>(&_peers);
		m_peers.reserve(peers.size());

		try {
			for (const auto &pair : peers) {
				Dictionary peerInfo = boost::any_cast<Dictionary>(pair.second);
				rawConnectPeer(peerInfo);
			}
		} catch (const std::exception &e) {
			// cast error
			std::cerr << m_meta.name() << ": connectToPeers(dict): " << e.what() << std::endl;
		}
	} else if (_peers.type() == typeid(VectorType)) {
		VectorType peers = *boost::unsafe_any_cast<VectorType>(&_peers);
		m_peers.reserve(peers.size());

		try {
			for (const boost::any &any : peers) {
				Dictionary peerInfo = boost::any_cast<Dictionary>(any);
				rawConnectPeer(peerInfo);
			}
		} catch (const std::exception &e) {
			// cast error
			std::cerr << m_meta.name() << ": connectToPeers(vec): " << e.what() << std::endl;
		}
	} else {
		std::cerr << m_meta.name() << ": peers type unhandled: " << _peers.type().name() << std::endl;
		return;	// FIXME
	}
}

void Torrent::addPeer(const PeerPtr &peer)
{
	auto it = m_blacklisted.find(peer->ip());
	if (it != m_blacklisted.end())
		m_blacklisted.erase(it);

	m_peers.insert(std::make_pair(peer->ip(), peer));
}

void Torrent::removePeer(const PeerPtr &peer, const std::string &errmsg)
{
	auto it = m_peers.find(peer->ip());
	if (it != m_peers.end())
		m_peers.erase(it);
}

void Torrent::disconnectPeers()
{
	for (auto it : m_peers)
		it.second->disconnect();
	m_peers.clear();
}

void Torrent::sendBitfield(const PeerPtr &peer)
{
	const bitset *b = m_fileManager.completedBits();
	if (b->count() == 0)
		return;

	uint8_t bits[b->size()];
	for (size_t i = 0; i < b->size(); ++i)
		if (b->test(i))
			bits[i] = b->bitsAt(i) & (1 << (7 - (i & 7)));
	peer->sendBitfield(&bits[0], b->size());
}

void Torrent::requestPiece(const PeerPtr &peer)
{
	size_t index = m_fileManager.getPieceforRequest([peer] (size_t i) { return peer->hasPiece(i); });
	if (index != std::numeric_limits<size_t>::max())
		peer->sendPieceRequest(index);
}

bool Torrent::handlePieceCompleted(const PeerPtr &peer, uint32_t index, DataBuffer<uint8_t> &&data)
{
	uint32_t downloaded = data.size();
	if (m_fileManager.writePieceBlock(index, peer->ip(), std::move(data))) {
		m_downloadedBytes += downloaded;
		return true;
	}

	m_wastedBytes += downloaded;
	++m_hashMisses;
	return false;
}

bool Torrent::handleRequestBlock(const PeerPtr &peer, uint32_t index, uint32_t begin, uint32_t length)
{
	return m_fileManager.requestPieceBlock(index, peer->ip(), begin, length);
}

void Torrent::onPieceWriteComplete(uint32_t from, uint32_t index)
{
	for (const auto &it : m_peers)
		it.second->sendHave(index);
}

void Torrent::onPieceReadComplete(uint32_t from, uint32_t index, uint32_t begin, uint8_t *block, size_t size)
{
	auto it = m_peers.find(from);
	if (it != m_peers.end()) {
		it->second->sendPieceBlock(index, begin, block, size);
		m_uploadedBytes += size;
	}
}

void Torrent::handleTrackerError(Tracker *tracker, const std::string &error)
{
}

void Torrent::handlePeerDebug(const PeerPtr &peer, const std::string &msg)
{
}

void Torrent::handleNewPeer(const PeerPtr &peer)
{
	m_peers.insert(std::make_pair(peer->ip(), peer));
	sendBitfield(peer);
}

