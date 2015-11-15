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
	  m_totalSize(0),
	  m_listener(nullptr)
{

}

Torrent::~Torrent()
{
	for (auto f : m_files)
		fclose(f.fp);
	m_files.clear();
	m_peers.clear();

	for (Tracker *tracker : m_activeTrackers)
		delete tracker;
	m_activeTrackers.clear();

	if (m_listener)
		m_listener->stop();
	delete m_listener;
}

bool Torrent::open(const std::string &fileName, const std::string &downloadDir)
{
	Bencode bencode;
	Dictionary dict = bencode.decode(fileName);

	if (dict.empty()) {
		std::cerr << fileName << ": unable to decode data" << std::endl;
		return false;
	}

	m_mainTracker = Bencode::cast<std::string>(dict["announce"]);
	if (dict.count("comment"))
		m_comment = Bencode::cast<std::string>(dict["comment"]);
	if (dict.count("announce-list"))
		m_trackers = Bencode::cast<VectorType>(dict["announce-list"]);

	Dictionary info = Bencode::cast<Dictionary>(dict["info"]);
	size_t pos = bencode.pos();
	bencode.encode(info);

	size_t bufferSize;
	const char *buffer = bencode.buffer(pos, bufferSize);

	uint8_t checkSum[20];
	boost::uuids::detail::sha1 sha1;
	sha1.process_bytes(buffer, bufferSize);

	uint32_t digest[5];
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

	m_name = Bencode::cast<std::string>(info["name"]);
	m_pieceLength = Bencode::cast<int64_t>(info["piece length"]);

	std::string pieces = Bencode::cast<std::string>(info["pieces"]);
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
		std::string dirName = Bencode::cast<std::string>(info["name"]);
		MKDIR(dirName);
		CHDIR(dirName);
		base = getcwd();	/* += PATH_SEP + dirName  */

		int64_t begin = 0;
		size_t index = 0;

		const boost::any &any = info["files"];
		if (any.type() == typeid(Dictionary)) {
			const Dictionary &files = Bencode::cast<Dictionary>(any);
			for (const auto &pair : files) {
				Dictionary v = Bencode::cast<Dictionary>(pair.second);
				VectorType pathList = Bencode::cast<VectorType>(v["path"]);
	
				if (!parseFile(std::move(v), std::move(pathList), index, begin))
					return false;
			}
		} else if (any.type() == typeid(VectorType)) {
			const VectorType &files = Bencode::cast<VectorType>(any);
			for (const auto &f : files) {
				Dictionary v = Bencode::cast<Dictionary>(f);
				VectorType pathList = Bencode::cast<VectorType>(v["path"]);

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

		int64_t length = Bencode::cast<int64_t>(info["length"]);
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
		const std::string &s = Bencode::cast<std::string>(*it);
		if (it == pathList.end() - 1) {
			path += s;
			break;
		}

		path += s + PATH_SEP;
		if (!nodeExists(path))
			MKDIR(path);
	}

	const int64_t length = Bencode::cast<int64_t>(v["length"]);
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
	size_t downloaded = computeDownloaded();
	return (double)(m_totalSize - downloaded) * elapsed_s / (double)downloaded;
}

double Torrent::downloadSpeed() const
{
	clock_t elapsed_s = elapsed() / CLOCKS_PER_SEC;
	size_t downloaded = computeDownloaded();
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

	uint32_t digest[5];
	sha1.get_digest(digest);
	for (int i = 0; i < 5; ++i)
		writeBE32(&checkSum[i * 4], digest[i]);	
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
	size_t fileEnd = f->begin + f->length;
	if (pieceBegin + pieceLength > fileEnd) {
		std::cerr << m_name << ": insane piece size" << std::endl;
		return;
	}

	uint8_t buf[m_pieceLength];
	if (pieceBegin < f->begin) {
		size_t bufPos = f->begin - pieceBegin;
		size_t read = fread(&buf[bufPos], 1, m_pieceLength - bufPos, fp);
		if (read != m_pieceLength - bufPos)
			return;

		while (bufPos != 0) {
			File *cf = &m_files[--index];
			FILE *fpp = cf->fp;

			if (bufPos > cf->length) {
				read = fread(&buf[bufPos - cf->length], 1, cf->length, fpp);
				if (read != cf->length)
					return;

				bufPos -= cf->length;
			} else {
				fseek(fpp, cf->length - bufPos, SEEK_SET);
				read = fread(&buf[0], 1, bufPos, fpp);
				if (read != bufPos)
					return;

				break;
			}
		}

		if (checkPieceHash(&buf[0], m_pieceLength, pieceIndex)) {
			++m_completedPieces;
			m_pieces[pieceIndex].done = true;
		}

		pieceBegin += pieceLength;
		++pieceIndex;
	}

	// seek
	fseek(fp, pieceBegin - f->begin, SEEK_SET);

	size_t i = 0;
	size_t real = pieceIndex;
	bool next_exit = false;
#pragma omp parallel for
	for (size_t i = pieceIndex; i < m_pieces.size(); ++i) {
		if (next_exit)
			continue;

		if (pieceBegin + m_pieceLength >= fileEnd) {
#pragma omp critical
			next_exit = true;
			real = i;
			continue;
		}

		size_t read = fread(&buf[0], 1, m_pieceLength, fp);
		if (checkPieceHash(&buf[0], read, i)) {
			++m_completedPieces;
			m_pieces[i].done = true;
		}

		pieceBegin += m_pieceLength;
	}

	pieceIndex = real;	// FIXME
	if (pieceIndex == m_pieces.size() - 1) {
		pieceLength = pieceSize(pieceIndex);
		if (fread(&buf[0], 1, pieceLength, fp) != pieceLength)
			return;

		if (checkPieceHash(&buf[0], pieceLength, pieceIndex)) {
			++m_completedPieces;
			m_pieces[pieceIndex].done = true;
		}
	}
}

TrackerQuery Torrent::makeTrackerQuery(TrackerEvent event) const
{
	size_t downloaded = computeDownloaded();
	TrackerQuery q = {
		.event = event,
		.downloaded = downloaded,
		.uploaded = m_uploadedBytes,
		.remaining = m_totalSize - downloaded
	};

	return q;
}

Torrent::DownloadError Torrent::download(uint16_t port)
{
	if (isFinished())
		return DownloadError::AlreadyDownloaded;

	if (!queryTrackers(makeTrackerQuery(TrackerEvent::Started), port))
		return DownloadError::TrackerQueryFailure;

	m_listener = new(std::nothrow) Server(port);
	m_startTime = clock();
	while (!isFinished()) {
		if (m_listener)
			m_listener->accept(
				[this] (const ConnectionPtr &c) {
					auto peer = std::make_shared<Peer>(c, this);
					peer->verify();
				}
			);

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
	return event == TrackerEvent::Completed ? DownloadError::Completed : DownloadError::NetworkError;
}

bool Torrent::seed(uint16_t port)
{
	if (!m_listener)
		m_listener = new(std::nothrow) Server(port);

	if (!m_listener)
		return false;

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
	bool success = queryTracker(m_mainTracker, query, port);
	if (m_trackers.empty()) {
		if (!success)
			std::cerr << m_name << ": queryTracker(): This torrent does not provide multiple trackers" << std::endl;

		return success;
	}

	bool alt = false;
	for (const boost::any &s : m_trackers) {
		if (s.type() == typeid(VectorType)) {
			const VectorType &vType = Bencode::cast<VectorType>(&s);
			for (const boost::any &announce : vType)
				alt = queryTracker(Bencode::cast<std::string>(&announce), query, port);
		} else if (s.type() == typeid(std::string))
			alt = queryTracker(Bencode::cast<std::string>(&s), query, port);
		else
			std::cerr << m_name << ": warning: unkown tracker type: " << s.type().name() << std::endl;
	}

	return success || alt;
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
		if (ip == 0)
			continue;

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

void Torrent::addPeer(const PeerPtr &peer)
{
	m_peers.push_back(peer);
	std::clog << m_name << ": " << peer->getIP() << ": connection successful (" << m_peers.size() << " established)" << std::endl;
}

void Torrent::removePeer(const PeerPtr &peer, const std::string &errmsg)
{
	auto it = std::find(m_peers.begin(), m_peers.end(), peer);
	if (it != m_peers.end())
		m_peers.erase(it);

	std::clog << m_name << ": " << peer->getIP() << ": " << errmsg << " (" << m_peers.size() << " established)" << std::endl;
}

void Torrent::disconnectPeers()
{
	for (const PeerPtr &peer : m_peers)
		peer->disconnect();
	m_peers.clear();
}

void Torrent::sendBitfield(const PeerPtr &peer)
{
	std::vector<uint8_t> bitfield;
	for (size_t i = 0; i < m_pieces.size(); i += 8) {
		uint8_t b = 0;
		for (uint8_t x = 7; x != 0; --x)
			if (m_pieces[x + i].done)
				b |= 1 << x;
		bitfield.push_back(b);
	}

	peer->sendBitfield(bitfield);
}

void Torrent::requestPiece(const PeerPtr &peer)
{
	size_t index = 0;
	int32_t priority = std::numeric_limits<int32_t>::max();

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

	if (priority != std::numeric_limits<int32_t>::max() && !m_pieces[index].done) {
		++m_pieces[index].priority;
		peer->sendPieceRequest(index);
	}	
}

void Torrent::handleTrackerError(Tracker *tracker, const std::string &error)
{
	std::cerr << m_name << ": " << tracker->host() << ":" << tracker->port() << ": tracker request failed: " << error << std::endl;
}

void Torrent::handlePeerDebug(const PeerPtr &peer, const std::string &msg)
{
	std::clog << m_name << ": " << peer->getIP() << " " << msg << std::endl;
}

bool Torrent::handlePieceCompleted(const PeerPtr &peer, uint32_t index, const DataBuffer<uint8_t> &pieceData)
{
	if (!checkPieceHash(&pieceData[0], pieceData.size(), index)) {
		std::cerr << m_name << ": " << peer->getIP() << " checksum mismatch for piece " << index << "." << std::endl;
		++m_hashMisses;
		m_wastedBytes += pieceData.size();
		return false;
	}

	m_pieces[index].done = true;
	m_downloadedBytes += pieceData.size();
	++m_completedPieces;

	int64_t beginPos = index * m_pieceLength;
	const uint8_t *data = &pieceData[0];
	size_t off = 0, size = pieceData.size();
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
	return true;
}

void Torrent::handleRequestBlock(const PeerPtr &peer, uint32_t index, uint32_t begin, uint32_t length)
{
	// Peer requested piece block from us
	if (index >= m_pieces.size() || !m_pieces[index].done)
		return peer->disconnect();

	size_t blockEnd = begin + length;
	if (blockEnd > pieceSize(index))
		return peer->disconnect();		// This peer is on drugs

	uint8_t block[length];
	size_t writePos = 0;
	size_t blockBegin = begin + (index * m_pieceLength);

	// Figure out which file has that piece
	for (const File &file : m_files) {
		size_t filePos = blockBegin + writePos;
		if (filePos < file.begin)
			break;

		size_t fileEnd = file.begin + file.length;
		if (filePos > fileEnd)
			continue;

		// check if that file was open for read, if so no need to re-open.
		errno = 0;
		FILE *fp = file.fp;
		uint8_t c = fgetc(fp);
		if (errno == EBADF && !(fp = fopen(file.path.c_str(), "rb"))) {
			std::cerr << m_name << ": handleRequestBlock(): unable to open: " << file.path.c_str() << ": " << strerror(errno) << std::endl;
			return;
		}

		// seek to where it begins
		fseek(fp, filePos - file.begin, SEEK_SET);

		// read up to file end but do not exceed requested buffer length
		size_t readSize = std::max(fileEnd - filePos, length - writePos);
		size_t max = writePos + readSize;
		while (writePos < max) {
			int read = fread(&block[writePos], 1, readSize - writePos, fp);
			if (read < 0) {
				fclose(fp);
				std::cerr << m_name << ": handleRequestBlock(): unable to read from: " << file.path.c_str() << std::endl;
				return;
			}

			writePos += read;
		}

		fclose(fp);
	}

	peer->sendPieceBlock(index, begin, block, length);
	m_uploadedBytes += length;
}

void Torrent::handleNewPeer(const PeerPtr &peer)
{
	m_peers.push_back(peer);
	sendBitfield(peer);
	std::clog << m_name << ": " << peer->getIP() << ": connected! (" << m_peers.size() << " established)" << std::endl;
}

size_t Torrent::computeDownloaded() const
{
	size_t downloaded = 0;

	for (size_t i = 0; i < m_pieces.size(); ++i)
		if (m_pieces[i].done)
			downloaded += pieceSize(i);
	return downloaded;
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

