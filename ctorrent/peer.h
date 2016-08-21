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
#ifndef __PEER_H
#define __PEER_H

#include <net/connection.h>
#include <net/inputmessage.h>

#include <memory>
#include <vector>

#include <util/bitset.h>

class Torrent;
class Peer : public std::enable_shared_from_this<Peer>
{
	struct Piece;
	enum State : uint8_t {
		PS_AmChoked = 1 << 0,			// We choked this peer (aka we're not giving him anymore pieces)
		PS_AmInterested = 1 << 1,		// We're interested in this peer's pieces
		PS_PeerChoked = 1 << 2,			// Peer choked us
		PS_PeerInterested = 1 << 3,		// Peer interested in our stuff
	};

	enum MessageType : uint8_t {
		MT_Choke		= 0,
		MT_UnChoke		= 1,
		MT_Interested		= 2,
		MT_NotInterested	= 3,
		MT_Have			= 4,
		MT_Bitfield		= 5,
		MT_Request		= 6,
		MT_PieceBlock		= 7,
		MT_Cancel		= 8,
		MT_Port			= 9
	};

public:
	Peer(Torrent *t);
	Peer(const ConnectionPtr &c, Torrent *t);
	~Peer();

	inline void setId(const std::string &id) { m_peerId = id; }
	inline std::string getIP() const { return m_conn->getIPString(); }
	inline uint32_t ip() const { return m_conn->getIP(); }
	void disconnect();
	void connect(const std::string &ip, const std::string &port);

protected:
	void verify();
	void handle(const uint8_t *data, size_t size);
	void handleMessage(MessageType mType, InputMessage in);
	void handleError(const std::string &errmsg);
	void handlePieceBlockData(size_t index, size_t begin, const uint8_t *block, size_t size);

	void sendKeepAlive();
	void sendChoke();
	void sendUnchoke();
	void sendBitfield(const uint8_t *bits, size_t size);
	void sendHave(uint32_t index);
	void sendPieceBlock(uint32_t index, uint32_t begin, const uint8_t *block, size_t size);
	void sendPieceRequest(uint32_t index);
	void sendRequest(uint32_t index, uint32_t begin, uint32_t size);
	void sendInterested();
	void sendCancel(uint32_t index, uint32_t begin, uint32_t size);

	void requestPiece(size_t pieceIndex);
	void cancelPiece(Piece *p);

	inline bool hasPiece(size_t i) const { return m_bitset.test(i); }
	inline bool isRemoteChoked() const { return test_bit(m_state, PS_PeerChoked); }
	inline bool isLocalChoked() const  { return test_bit(m_state, PS_AmChoked); }

	inline bool isRemoteInterested() const { return test_bit(m_state, PS_PeerInterested); }
	inline bool isLocalInterested() const { return test_bit(m_state, PS_AmInterested); }

private:
	struct PieceBlock {
		size_t size;
		uint8_t *data;

		PieceBlock() { data = nullptr; size  = 0; }
		~PieceBlock() { delete []data; }
	};

	struct Piece {
		size_t index;
		size_t currentBlocks;
		size_t numBlocks;

		~Piece() { delete []blocks; }
		PieceBlock *blocks;
	};

	struct PieceBlockInfo {
		size_t index;
		size_t begin;
		size_t length;

		PieceBlockInfo(size_t i, int64_t beg, int64_t len)
			: index(i),
			  begin(beg),
			  length(len)
		{
		}

		bool operator==(const PieceBlockInfo &other) const
		{
			return other.index == index
				&& other.begin == begin
				&& other.length == length;
		}
	};

	bitset m_bitset;
	std::vector<Piece *> m_queue;
	std::vector<PieceBlockInfo> m_requestedBlocks;
	std::string m_peerId;

	uint8_t m_state;
	uint32_t m_eventId;

	Torrent *m_torrent;
	ConnectionPtr m_conn;

	friend class Torrent;
};
typedef std::shared_ptr<Peer> PeerPtr;

#endif

