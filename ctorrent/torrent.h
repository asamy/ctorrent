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

#include <boost/any.hpp>
#include <bencode/bencode.h>
#include <net/server.h>

#include <vector>
#include <map>
#include <string>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iosfwd>

static size_t maxRequestSize = 16384;		// 16KiB initial (per piece)
class Torrent
{
private:
	struct File {
		std::string path;
		FILE *fp;
		int64_t begin;		// offset of which the piece(s) begin
		int64_t length;
	};

public:
	enum class DownloadError {
		Completed		 = 0,
		TrackerQueryFailure	 = 1,
		AlreadyDownloaded	 = 2,
		NetworkError		 = 3
	};

	Torrent();
	~Torrent();

	bool open(const std::string& fileName, const std::string &downloadDir);
	DownloadError download(uint16_t port);
	bool seed(uint16_t port);

	inline bool isFinished() const { return m_completedPieces == m_pieces.size(); }
	inline size_t activePeers() const { return m_peers.size(); }
	inline int64_t totalSize() const { return m_totalSize; }
	inline int64_t downloadedBytes() const { return m_downloadedBytes; }
	inline uint32_t uploadedBytes() const { return m_uploadedBytes; }
	inline size_t totalPieces() const { return m_pieces.size(); }
	inline size_t completedPieces() const { return m_completedPieces; }
	inline std::string name() const { return m_name; }

	double eta() const;
	double downloadSpeed() const;
	clock_t elapsed() const;

protected:
	bool checkPieceHash(const uint8_t *data, size_t size, uint32_t index);
	bool queryTrackers(const TrackerQuery &r, uint16_t port);
	bool queryTracker(const std::string &url, const TrackerQuery &r, uint16_t port);
	bool parseFile(Dictionary &&v, VectorType &&pathList, size_t &index, int64_t &begin);
	void findCompletedPieces(const struct File *f, size_t index);
	void rawConnectPeers(const uint8_t *peers, size_t size);
	void connectToPeers(const boost::any &peers);
	void sendBitfield(const PeerPtr &peer);
	void requestPiece(const PeerPtr &peer);
	size_t computeDownloaded() const;
	int64_t pieceSize(size_t pieceIndex) const;
	inline bool pieceDone(size_t pieceIndex) const { return m_pieces[pieceIndex].done; }

	void addPeer(const PeerPtr &peer);
	void removePeer(const PeerPtr &peer, const std::string &errmsg);
	void disconnectPeers();

	const uint8_t *peerId() const { return m_peerId; }
	const uint8_t *handshake() const { return m_handshake; }

	TrackerQuery makeTrackerQuery(TrackerEvent event) const;
	void handleTrackerError(const TrackerPtr &tracker, const std::string &error);
	void handlePeerDebug(const PeerPtr &peer, const std::string &msg);
	void handlePieceCompleted(const PeerPtr &peer, uint32_t index, const DataBuffer<uint8_t> &data);
	void handleRequestBlock(const PeerPtr &peer, uint32_t index, uint32_t begin, uint32_t length);
	void handleNewPeer(const PeerPtr &peer);

private:
	struct Piece {
		bool done;
		int32_t priority;
		uint8_t hash[20];
	};	

	std::vector<TrackerPtr> m_activeTrackers;
	std::vector<PeerPtr> m_peers;
	std::vector<Piece> m_pieces;
	std::vector<File> m_files;

	size_t m_completedPieces;
	size_t m_uploadedBytes;
	size_t m_downloadedBytes;
	size_t m_wastedBytes;
	size_t m_hashMisses;

	size_t m_pieceLength;
	size_t m_totalSize;

	Server *m_listener;
	VectorType m_trackers;
	std::string m_name;
	std::string m_mainTracker;
	std::string m_comment;

	clock_t m_startTime;
	uint8_t m_handshake[68];
	uint8_t m_peerId[20];

	friend class Peer;
	friend class Tracker;
};

#endif

