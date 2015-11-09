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

#include <algorithm>
#include <iostream>
#include <thread>

#include <boost/uuid/sha1.hpp>

Torrent::Torrent()
	: m_completedPieces(0),
	  m_uploadedBytes(0),
	  m_downloadedBytes(0),
	  m_wastedBytes(0),
	  m_hashMisses(0),
	  m_pieceLength(0),
	  m_totalSize(0)
{

}

Torrent::~Torrent()
{
	for (auto f : m_files)
		fclose(f.fp);
	m_files.clear();
	m_activeTrackers.clear();
}

bool Torrent::open(const std::string &fileName, const std::string &downloadDir)
{
	Bencode bencode;
	Dictionary dict = bencode.decode(fileName);

	if (dict.empty()) {
		std::cerr << fileName << ": unable to decode data" << std::endl;
		return false;
	}

	m_mainTracker = bencode.unsafe_cast<std::string>(dict["announce"]);
	if (dict.count("comment"))
		m_comment = bencode.unsafe_cast<std::string>(dict["comment"]);
	if (dict.count("announce-list"))
		m_trackers = bencode.unsafe_cast<VectorType>(dict["announce-list"]);

	Dictionary info = bencode.unsafe_cast<Dictionary>(dict["info"]);
	size_t pos = bencode.pos();
	bencode.encode(info);

	size_t bufferSize;
	const char *buffer = bencode.buffer(pos, bufferSize);

	uint8_t checkSum[20];
	boost::uuids::detail::sha1 sha1;
	sha1.process_bytes(buffer, bufferSize);

	unsigned int digest[5];
	sha1.get_digest(digest);
	for (int i = 0; i < 5; ++i)
		writeBE32(&checkSum[i * 4], digest[i]);	

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

	m_name = bencode.unsafe_cast<std::string>(info["name"]);
	m_pieceLength = bencode.unsafe_cast<int64_t>(info["piece length"]);

	std::string pieces = bencode.unsafe_cast<std::string>(info["pieces"]);
	for (size_t i = 0; i < pieces.size(); i += 20) {
		Piece piece;
		piece.done = false;
		piece.priority = 0;
		memcpy(&piece.hash[0], pieces.c_str() + i, 20);
		m_pieces.push_back(piece);
	}

	MKDIR(downloadDir);
	chdir(downloadDir.c_str());

	std::string base = getcwd();
	if (info.count("files") > 0) {
		std::string dirName = bencode.unsafe_cast<std::string>(info["name"]);
		MKDIR(dirName);
		CHDIR(dirName);

		base = getcwd();	/* += PATH_SEP + dirName  */
		const boost::any &any = info["files"];
		int64_t begin = 0;
		size_t index = 0;
		if (any.type() == typeid(Dictionary)) {
			const Dictionary &files = bencode.unsafe_cast<Dictionary>(any);
			for (const auto &pair : files) {
				Dictionary v = bencode.unsafe_cast<Dictionary>(pair.second);
				VectorType pathList = bencode.unsafe_cast<VectorType>(v["path"]);
	
				if (!parseFile(std::move(v), std::move(pathList), index, begin))
					return false;
			}
		} else if (any.type() == typeid(VectorType)) {
			const VectorType &files = bencode.unsafe_cast<VectorType>(any);
			for (const auto &f : files) {
				Dictionary v = bencode.unsafe_cast<Dictionary>(f);
				VectorType pathList = bencode.unsafe_cast<VectorType>(v["path"]);

				if (!parseFile(std::move(v), std::move(pathList), index, begin))
					return false;
			}
		} else {
			// This can happen?
			std::cerr << m_name << ": Invalid info files type" << std::endl;
		}	

		chdir("..");
	} else {
		// Just one file
		if (!validatePath(base, base + "/" + m_name)) {
			std::cerr << m_name << ": error validating path: " << base << "/" << m_name << std::endl;
			return false;
		}

		int64_t length = bencode.unsafe_cast<int64_t>(info["length"]);
		File file = {
			.path = m_name.c_str(),
			.fp = nullptr,
			.begin = 0,
			.length = length
		};
		if (!nodeExists(m_name.c_str())) {
			/* Open for writing and create  */
			file.fp = fopen(m_name.c_str(), "wb");
			if (!file.fp) {
				std::cerr << m_name << ": unable to create " << m_name << std::endl;
				return false;
			}
		} else {
			/* Open for both reading and writing  */
			file.fp = fopen(m_name.c_str(), "rb+");
			if (!file.fp) {
				std::cerr << m_name << ": unable to open " << m_name << std::endl;
				return false;
			}

			findCompletedPieces(&file, 0);
			std::clog << m_name << ": Completed pieces: " << m_completedPieces << "/" << m_pieces.size() << std::endl;
		}

		m_totalSize = length;
		m_files.push_back(file);
	}

	chdir("..");
	return true;
}

bool Torrent::parseFile(Dictionary &&v, VectorType &&pathList, size_t &index, int64_t &begin)
{
	std::string path;

	for (auto it = pathList.begin();; ++it) {
		const std::string &s = Bencode::unsafe_cast<std::string>(*it);
		if (it == pathList.end() - 1) {
			path += s;
			break;
		}

		path += s + PATH_SEP;
		if (!nodeExists(path))
			MKDIR(path);
	}

	const int64_t length = Bencode::unsafe_cast<int64_t>(v["length"]);
	File file = {
		.path = path,
		.fp = nullptr,
		.begin = begin,
		.length = length
	};
	if (!nodeExists(path)) {
		/* Open for writing and create  */
		file.fp = fopen(path.c_str(), "wb");
		if (!file.fp) {
			std::cerr << m_name << ": unable to create " << path << std::endl;
			return false;
		}
	} else {
		/* Open for both writing and reading  */
		file.fp = fopen(path.c_str(), "rb+");
		if (!file.fp) {
			std::cerr << m_name << ": unable to open " << path << std::endl;
			return false;
		}

		findCompletedPieces(&file, index);
		std::clog << m_name << ": " << path << ": Completed pieces: " << m_completedPieces << "/" << m_pieces.size() << std::endl;
	}

	m_files.push_back(file);
	m_totalSize += length;
	begin += length;
	++index;
	return true;
}

double Torrent::eta() const
{
	clock_t elapsed_s = elapsed() / CLOCKS_PER_SEC;
	size_t downloaded = 0;

	for (size_t i = 0; i < m_pieces.size(); ++i)
		if (m_pieces[i].done)
			downloaded += pieceSize(i);

	return (double)(m_totalSize - downloaded) * elapsed_s / (double)downloaded;
}

double Torrent::downloadSpeed() const
{
	clock_t elapsed_s = elapsed() / CLOCKS_PER_SEC;
	size_t downloaded = 0;

	for (size_t i = 0; i < m_pieces.size(); ++i)
		if (m_pieces[i].done)
			downloaded += pieceSize(i);

	if (elapsed_s == 0)
		return 0.0f;

	double speed = (double)(downloaded / elapsed_s);
	return (speed / 1024) * 0.0125;
}

clock_t Torrent::elapsed() const
{
	return clock() - m_startTime;
}

bool Torrent::checkPieceHash(const uint8_t *data, size_t size, uint32_t index)
{
	if (index >= m_pieces.size())
		return false;

	uint8_t checkSum[20];
	boost::uuids::detail::sha1 sha1;
	sha1.process_bytes(data, size);

	unsigned int sum[5];
	sha1.get_digest(sum);
	for (int i = 0; i < 5; ++i)
		writeBE32(&checkSum[i * 4], sum[i]);	
	return memcmp(checkSum, m_pieces[index].hash, 20) == 0;
}

void Torrent::findCompletedPieces(const File *f, size_t index)
{
	FILE *fp = f->fp;
	fseek(fp, 0L, SEEK_END);
	off_t fileLength = ftello(fp);
	fseek(fp, 0L, SEEK_SET);

	if (fileLength > f->length) {
		std::clog << m_name << ": Truncating " << f->path << " to " << bytesToHumanReadable(f->length, true) << std::endl;
		truncate(f->path.c_str(), f->length);

		return;	// file truncated
	}

	size_t pieceIndex = 0;	// start piece index
	if (f->begin > 0)	// not first file?
		pieceIndex = f->begin / m_pieceLength;

	size_t pieceBegin = pieceIndex * m_pieceLength;
	size_t pieceLength = pieceSize(pieceIndex);
	int64_t fileEnd = f->begin + f->length;
	if (pieceBegin + pieceLength > fileEnd) {
		std::cerr << m_name << ": insane piece size" << std::endl;
		return;
	}
}

TrackerQuery Torrent::makeTrackerQuery(TrackerEvent event) const
{
	TrackerQuery q;

	q.event = event;
	q.uploaded = 0;
	q.downloaded = 0;

	for (size_t i = 0; i < m_pieces.size(); ++i)
		if (m_pieces[i].done)
			q.downloaded += pieceSize(i);
	q.remaining = m_totalSize - q.downloaded;
	return q;
}

Torrent::DownloadError Torrent::download(uint16_t port)
{
	size_t piecesNeeded = m_pieces.size();
	if (m_completedPieces == piecesNeeded)
		return DownloadError::AlreadyDownloaded;

	m_startTime = clock();
	if (!queryTrackers(makeTrackerQuery(TrackerEvent::Started), port))
		return DownloadError::TrackerQueryFailure;

	while (m_completedPieces != piecesNeeded) {
		for (const TrackerPtr &tracker : m_activeTrackers)
			if (tracker->timeUp())
				tracker->query(makeTrackerQuery(TrackerEvent::None));
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	for (const auto &f : m_files)
		fclose(f.fp);
	m_files.clear();

	TrackerEvent event = m_completedPieces == piecesNeeded ? TrackerEvent::Completed : TrackerEvent::Stopped;
	TrackerQuery q = makeTrackerQuery(event);
	for (const TrackerPtr &tracker : m_activeTrackers)
		tracker->query(q);

	return event == TrackerEvent::Completed ? DownloadError::Completed : DownloadError::NetworkError;
}

bool Torrent::queryTrackers(const TrackerQuery &query, uint16_t port)
{
	bool success = queryTracker(m_mainTracker, query, port);
	if (m_trackers.empty()) {
		if (!success)
			std::cerr << m_name << ": queryTracker(): This torrent does not provide multiple trackers" << std::endl;

		return success;
	}

	for (const boost::any &s : m_trackers) {
		if (s.type() == typeid(VectorType)) {
			const VectorType &vType = *boost::unsafe_any_cast<VectorType>(&s);
			for (const boost::any &announce : vType)
				if (!success)
					success = queryTracker(Bencode::unsafe_cast<std::string>(&announce), query, port);
		} else if (s.type() == typeid(std::string)) {
			if (!success)
				success = queryTracker(Bencode::unsafe_cast<std::string>(&s), query, port);
		} else {
			// This can actually happen?
			std::cerr << m_name << ": warning: unkown tracker type: " << s.type().name() << std::endl;
		}
	}

	return success;
}

bool Torrent::queryTracker(const std::string &furl, const TrackerQuery &q, uint16_t tport)
{
	UrlData url = parseUrl(furl);
	std::string host = URL_HOSTNAME(url);
	if (host.empty()) {
		std::cerr << m_name << ": queryTracker(): failed to parse announce url: " << furl << std::endl;
		return false;
	}

	std::string port = URL_SERVNAME(url);
	std::string protocol = URL_PROTOCOL(url);

	TrackerPtr tracker(new Tracker(this, host, port, protocol, tport));
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
		uint32_t ip =isLittleEndian() ? readLE32(iport) : readBE32(iport);

		auto it = std::find_if(m_peers.begin(), m_peers.end(),
				[ip] (const PeerPtr &peer) { return peer->ip() == ip; });
		if (it != m_peers.end())
			continue;

		// Asynchronously connect to that peer, and do not add it to our
		// active peers list unless a connection was established successfully.
		auto peer = std::make_shared<Peer>(this);
		peer->connect(ip2str(ip), std::to_string(readBE16(iport + 4)));
	}
}

void Torrent::connectToPeers(const boost::any &_peers)
{
	if (_peers.type() == typeid(std::string)) {
		std::string peers = *boost::unsafe_any_cast<std::string>(&_peers);
		m_peers.reserve(peers.length() / 6);

		return rawConnectPeers((const uint8_t *)peers.c_str(), peers.length());
	}

	if (_peers.type() == typeid(Dictionary)) {	// no compat
		Dictionary peers = *boost::unsafe_any_cast<Dictionary>(&_peers);
		m_peers.reserve(peers.size());

		try {
			for (const auto &pair : peers) {
				Dictionary peerInfo = boost::any_cast<Dictionary>(pair.second);
				std::string peerId = boost::any_cast<std::string>(peerInfo["peer id"]);
				std::string ip = boost::any_cast<std::string>(peerInfo["ip"]);
				int64_t port = boost::any_cast<int64_t>(peerInfo["port"]);

				auto it = std::find_if(m_peers.begin(), m_peers.end(),
						[ip] (const PeerPtr &peer) { return peer->getIP() == ip; });
				if (it != m_peers.end())
					continue;

				// Asynchronously connect to that peer, and do not add it to our
				// active peers list unless a connection was established successfully.
				auto peer = std::make_shared<Peer>(this);
				peer->setId(peerId);
				peer->connect(ip, std::to_string(port));
			}
		} catch (const std::exception &e) {
			std::cerr << m_name << ": connectToPeers(dict): " << e.what() << std::endl;
		}
	} else {
		// this can happen?
		std::cerr << m_name << ": peers type unhandled: " << _peers.type().name() << std::endl;
	}
}

void Torrent::addTracker(const std::string &tracker)
{
	m_trackers.push_back(tracker);
}

void Torrent::addPeer(const PeerPtr &peer)
{
	m_peers.push_back(peer);
	std::clog << m_name << ": Peers: " << m_peers.size() << std::endl;
}

void Torrent::removePeer(const PeerPtr &peer, const std::string &errmsg)
{
	auto it = std::find(m_peers.begin(), m_peers.end(), peer);
	if (it != m_peers.end())
		m_peers.erase(it);

	std::clog << m_name << ": " << peer->getIP() << ": " << errmsg << std::endl;
	std::clog << m_name << ": Peers: " << m_peers.size() << std::endl;
}

void Torrent::disconnectPeers()
{
	for (const PeerPtr &peer : m_peers)
		peer->disconnect();
	m_peers.clear();
}

void Torrent::requestPiece(const PeerPtr &peer)
{
	size_t index = 0;
	int32_t priority = std::numeric_limits<int32_t>::max();

#if 0
	std::vector<size_t> peerPieces = peer->getPieces();
	for (size_t i = 0; i < peerPieces.size(); ++i) {
		size_t piece = peerPieces[i];
		if (piece >= m_pieces.size())
			continue;

		Piece *p = &m_pieces[piece];
		if (p->done)
			continue;

		if (!p->priority) {
			p->priority = 1;
			return peer->sendPieceRequest(piece);
		}

		if (priority > p->priority) {
			priority = p->priority;
			index = piece;
		}
	}
#else
	for (size_t i = 0; i < m_pieces.size(); ++i) {
		Piece *p = &m_pieces[i];
		if (p->done || !peer->hasPiece(i))
			continue;

		if (!p->priority) {
			p->priority = 1;
			return peer->sendPieceRequest(i);
		}

		if (priority > p->priority) {
			priority = p->priority;
			index = i;
		}
	}
#endif

	if (priority != std::numeric_limits<int32_t>::max()) {
		++m_pieces[index].priority;
		peer->sendPieceRequest(index);
	}	
}

void Torrent::handleTrackerError(const TrackerPtr &tracker, const std::string &error)
{
	std::cerr << m_name << ": tracker request failed: " << error << std::endl;
}

void Torrent::handlePeerDebug(const PeerPtr &peer, const std::string &msg)
{
	std::clog << m_name << ": " << peer->getIP() << " " << msg << std::endl;
}

void Torrent::handlePieceCompleted(const PeerPtr &peer, uint32_t index, const DataBuffer<uint8_t> &pieceData)
{
	if (!checkPieceHash(&pieceData[0], pieceData.size(), index)) {
		std::cerr << m_name << ": " << peer->getIP() << " checksum mismatch for piece " << index << "." << std::endl;
		++m_hashMisses;
		m_wastedBytes += pieceData.size();
		return;
	}

	m_pieces[index].done = true;
	m_downloadedBytes += pieceData.size();
	++m_completedPieces;

	int64_t beginPos = index * m_pieceLength;
	const uint8_t *data = &pieceData[0];
	size_t off = 0;
	size_t size = pieceData.size();
	for (const File &file : m_files) {
		if (beginPos < file.begin)
			break;

		int64_t fileEnd = file.begin + file.length;
		if (beginPos >= fileEnd)
			break;

		int64_t amount = fileEnd - beginPos;
		if (amount > size)
			amount = size;

		fseek(file.fp, beginPos - file.begin, SEEK_SET);
		size_t wrote = fwrite(data + off, 1, amount, file.fp);
		off += wrote;
		size -= wrote;
		beginPos += wrote;
	}

	for (const PeerPtr &peer : m_peers)
		peer->sendHave(index);

	std::clog << m_name << ": " << peer->getIP() << " Completed " << m_completedPieces << "/" << m_pieces.size() << " pieces "
		<< "(Downloaded: " << bytesToHumanReadable(m_downloadedBytes, true) << ", Wasted: " << bytesToHumanReadable(m_wastedBytes, true) << ", "
		<< "Hash miss: " << m_hashMisses << ")"
		<< std::endl
		<< m_name << ": Download speed: " << downloadSpeed() << " Mbps, ETA: " << eta() << " seconds"
		<< std::endl;
}

void Torrent::handleRequestBlock(const PeerPtr &peer, uint32_t index, uint32_t begin, uint32_t length)
{
	// Peer requested block from us
	if (index >= m_pieces.size() || !m_pieces[index].done)
		return;		// We haven't finished downloading that piece yet, so we can't give it out.

	size_t blockEnd = begin + length;
	if (blockEnd > pieceSize(index))
		return peer->disconnect();		// This peer is on drugs

	uint8_t block[length];
	m_uploadedBytes += length;
	/// TODO
}

int64_t Torrent::pieceSize(size_t pieceIndex) const
{
	// Last piece can be different in size
	if (pieceIndex == m_pieces.size() - 1) {
		int64_t r = m_totalSize % m_pieceLength;
		if (r != 0)
			return r;
	}

	return m_pieceLength;
}

