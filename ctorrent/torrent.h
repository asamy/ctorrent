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
#ifndef __TORRENT_H
#define __TORRENT_H

#include "peer.h"
#include "tracker.h"
#include "torrentmeta.h"
#include "torrentfilemanager.h"

#include <boost/any.hpp>
#include <bencode/bencode.h>
#include <net/server.h>

#include <vector>
#include <map>
#include <string>
#include <chrono>
#include <iosfwd>

#include <unordered_map>
#include <unordered_set>

static size_t maxRequestSize = 16384;		// 16KiB initial (per piece)
class Torrent
{
public:
	enum class DownloadState {
		None			 = 0,
		Completed		 = 1,
		TrackerQueryFailure	 = 2,
		AlreadyDownloaded	 = 3,
		NetworkError		 = 4
	};

	Torrent();
	~Torrent();

	DownloadState prepare(uint16_t port);
	void checkTrackers();
	bool open(const std::string& fileName, const std::string &downloadDir);
	bool seed(uint16_t port);
	bool finish();
	bool isFinished() const { return m_fileManager.totalPieces() == m_fileManager.completedPieces(); }
	bool hasTrackers() const { return !m_activeTrackers.empty(); }

	size_t activePeers() const { return m_peers.size(); }
	size_t downloadedBytes() const { return m_downloadedBytes; }
	size_t uploadedBytes() const { return m_uploadedBytes; }
	size_t wastedBytes() const { return m_wastedBytes; }
	size_t hashMisses() const { return m_hashMisses; }

	double eta();
	double downloadSpeed();
	clock_t elapsed();

	// Get associated meta info for this torrent
	const TorrentMeta *meta() const { return &m_meta; }

	// Get associated file manager for this torrent
	const TorrentFileManager *fileManager() const { return &m_fileManager; }

protected:
	bool queryTrackers(const TrackerQuery &r, uint16_t port);
	bool queryTracker(const std::string &url, const TrackerQuery &r, uint16_t port);
	void rawConnectPeers(const uint8_t *peers, size_t size);
	void rawConnectPeer(Dictionary &peerInfo);
	void connectToPeers(const boost::any &peers);
	void sendBitfield(const PeerPtr &peer);
	void requestPiece(const PeerPtr &peer);

	TrackerQuery makeTrackerQuery(TrackerEvent event);
	size_t computeDownloaded() { return m_fileManager.computeDownloaded(); }

	void addBlacklist(uint32_t ip);
	void addPeer(const PeerPtr &peer);
	void removePeer(const PeerPtr &peer, const std::string &errmsg);
	void disconnectPeers();

	const uint8_t *peerId() const { return m_peerId; }
	const uint8_t *handshake() const { return m_handshake; }

	// Peer -> Torrent
	void handleTrackerError(Tracker *tracker, const std::string &error);
	void handlePeerDebug(const PeerPtr &peer, const std::string &msg);
	void handleNewPeer(const PeerPtr &peer);
	bool handlePieceCompleted(const PeerPtr &peer, uint32_t index, DataBuffer<uint8_t> &&data);
	bool handleRequestBlock(const PeerPtr &peer, uint32_t index, uint32_t begin, uint32_t length);

public:
	// TorrentFileManager -> Torrent
	void onPieceWriteComplete(uint32_t from, uint32_t index);
	void onPieceReadComplete(uint32_t from, uint32_t index, uint32_t begin, uint8_t *block, size_t size);

private:
	Server *m_listener;
	TorrentMeta m_meta;
	TorrentFileManager m_fileManager;

	std::vector<Tracker *> m_activeTrackers;
	std::unordered_map<uint32_t, PeerPtr> m_peers;
	std::unordered_set<uint32_t> m_blacklisted;

	size_t m_uploadedBytes;
	size_t m_downloadedBytes;
	size_t m_wastedBytes;
	size_t m_hashMisses;

	clock_t m_startTime;
	uint8_t m_handshake[68];
	uint8_t m_peerId[20];

	friend class Peer;
	friend class Tracker;
};

#endif

