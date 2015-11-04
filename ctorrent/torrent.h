/*
 * Copyright (c) 2014 Ahmed Samy  <f.fallen45@gmail.com>
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

#include <boost/any.hpp>

#include <vector>
#include <map>
#include <string>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iosfwd>

#include <bencode/bencode.h>

static int64_t maxRequestSize = 16384;		// 16KiB (per piece)

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
	DownloadError download(int port);

	inline size_t activePeers() const { return m_peers.size(); }
	inline int64_t totalSize() const { return m_totalSize; }
	int64_t downloadedBytes() const;
	inline uint32_t uploadedBytes() const { return m_uploadedBytes; }
	inline std::string name() const { return m_name; }

protected:
	bool checkPieceHash(const uint8_t *data, size_t size, uint32_t index);
	bool queryTracker(Dictionary &request, int port);
	void findCompletedPieces(const struct File *f, size_t index);
	void connectToPeers(const boost::any &peers);
	void requestPiece(const PeerPtr &peer, size_t pieceIndex);
	void requestPiece(const PeerPtr &peer);
	int64_t pieceSize(size_t pieceIndex) const;
	inline bool pieceDone(size_t pieceIndex) const { return m_pieces[pieceIndex].done; }

	bool connectTo(
		const std::string &host, const std::string &service,
		asio::ip::tcp::socket &socket
	);
	void addTracker(const std::string &tracker);
	void addPeer(const PeerPtr &peer);
	void removePeer(const PeerPtr &peer, const std::string &errmsg);
	inline void disconnectPeers();
	inline const uint8_t *peerId() const { return m_peerId; }
	inline const uint8_t *handshake() const { return m_handshake; }
	inline size_t totalPieces() const { return m_pieces.size(); }
	inline size_t completedPieces() const { return m_completedPieces; }

	void handlePieceCompleted(const PeerPtr &peer, uint32_t index, const DataBuffer<uint8_t> &data);
	void handleRequestBlock(const PeerPtr &peer, uint32_t index, uint32_t begin, uint32_t length);

private:
	struct Piece {
		bool done;
		int32_t priority;
		uint8_t hash[20];
	};	

	std::vector<PeerPtr> m_peers;
	std::vector<Piece> m_pieces;
	std::vector<File> m_files;

	uint32_t m_completedPieces;
	uint32_t m_uploadedBytes;
	uint32_t m_downloadedBytes;

	int64_t m_pieceLength;
	int64_t m_totalSize;

	size_t m_hashMisses;
	size_t m_pieceMisses;

	VectorType m_trackers;
	std::string m_name;
	std::string m_mainTracker;
	std::string m_comment;

	std::chrono::time_point<std::chrono::system_clock> m_timeToNextRequest;
	std::atomic_bool m_paused;
	uint8_t m_handshake[68];
	uint8_t m_peerId[20];

	friend class Peer;
};

#endif
