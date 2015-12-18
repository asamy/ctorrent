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
#include "torrentfilemanager.h"
#include "torrent.h"

#include <util/auxiliar.h>
#include <util/scheduler.h>

#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

#include <deque>
#include <queue>

#include <boost/uuid/sha1.hpp>

struct TorrentFile {
	FILE *fp;
	TorrentFileInfo info;
};

struct ReadRequest {
	size_t index;
	uint32_t id;
	uint32_t from;
	int64_t begin;
	int64_t size;
};
struct LeastReadRequest {
	bool operator() (const ReadRequest &lhs, const ReadRequest &rhs) const {
		return lhs.id > rhs.id;
	}
};

struct WriteRequest {
	size_t index;
	uint32_t from;
	DataBuffer<uint8_t> data;
};

struct Piece {
	Piece(const uint8_t *h) {
		memcpy(&hash[0], h, sizeof(hash));
		pri = 0;
	}

	int32_t pri;	// TODO Support more piece download strategies
	uint8_t hash[20];
};

class TorrentFileManagerImpl {
public:
	TorrentFileManagerImpl(Torrent *t) {
		m_torrent = t;
		m_stopped = false;
		m_thread = std::thread(std::bind(&TorrentFileManagerImpl::thread, this));
	}

	~TorrentFileManagerImpl() {
		m_stopped = true;
		m_condition.notify_all();
		m_thread.join();
	}

public:
	void lock() { m_mutex.lock(); }
	void unlock() { m_mutex.unlock(); }
	void unlock_and_notify() { m_mutex.unlock(); m_condition.notify_all(); }

	void push_read(const ReadRequest &r) { m_readRequests.push(r); }
	void push_write(WriteRequest &&w) { m_writeRequests.push(std::move(w)); }
	void push_file(const TorrentFile &f) { m_files.push_back(f); }
	void scan_file(const TorrentFile &f);
	void scan_pieces();

	const bitset *completed_bits() const { return &m_completedBits; }
	size_t completed_pieces() const { return m_completedBits.count(); }
	size_t total_pieces() const { return m_pieces.size(); }
	size_t get_next_piece(const std::function<bool (size_t)> &fun);
	size_t compute_downloaded();

	bool piece_done(size_t index) const { return index < m_pieces.size() && m_completedBits.test(index); }
	bool intact(size_t index) const { return index < m_pieces.size(); }
	bool is_read_eligible(size_t index, int64_t end) const { return m_completedBits.test(index) && end < piece_length(index); }
	bool is_write_eligible(size_t index, const uint8_t *data, size_t size) const {
		boost::uuids::detail::sha1 sha1;
		sha1.process_bytes(data, size);

		uint32_t digest[5];
		sha1.get_digest(digest);

		uint8_t checkSum[20];
		for (int i = 0; i < 5; ++i)
			writeBE32(&checkSum[i * 4], digest[i]);	

		return memcmp(checkSum, m_pieces[index].hash, 20) == 0;
	}

	int64_t piece_length(size_t index) const {
		const TorrentMeta *meta = m_torrent->meta();
		if (index == m_pieces.size() - 1)
			return last_piece_length();

		return meta->pieceLength();
	}

	int64_t last_piece_length() const {
		const TorrentMeta *meta = m_torrent->meta();
		if (int64_t r = meta->totalSize() % meta->pieceLength())
			return r;

		return meta->pieceLength();
	}

	void init_pieces() {
		auto sha1sums = m_torrent->meta()->sha1sums();
		m_pieces.reserve(sha1sums.size());
		m_completedBits.construct(sha1sums.size());

		for (size_t i = 0; i < sha1sums.size(); ++i)
			m_pieces.push_back(Piece((const uint8_t *)sha1sums[i].c_str()));
	}

protected:
	void thread();	

	bool process_read(const ReadRequest &r);
	bool process_write(const WriteRequest &w);

private:
	std::priority_queue<ReadRequest, std::deque<ReadRequest>, LeastReadRequest> m_readRequests;
	std::queue<WriteRequest> m_writeRequests;

	bitset m_completedBits;
	std::vector<TorrentFile> m_files;
	std::vector<Piece> m_pieces;

	std::thread m_thread;
	std::mutex m_mutex;
	std::condition_variable m_condition;
	std::atomic_bool m_stopped;

	Torrent *m_torrent;
	friend class TorrentFileManager;
};

void TorrentFileManagerImpl::scan_file(const TorrentFile &f)
{
	FILE *fp = f.fp;
	fseek(fp, 0L, SEEK_END);
	off64_t fileLength = ftello64(fp);
	fseek(fp, 0L, SEEK_SET);

	if (f.info.length > fileLength) {
		truncate(f.info.path.c_str(), f.info.length);
		return;
	}

	size_t m_pieceLength = m_torrent->meta()->pieceLength();
	size_t pieceIndex = 0;
	if (f.info.begin > 0)
		pieceIndex = f.info.begin / m_pieceLength;

	size_t pieceBegin = pieceIndex * m_pieceLength;
	size_t pieceLength = piece_length(pieceIndex);
	size_t fileEnd = f.info.begin + f.info.length;
	if (pieceBegin + pieceLength > fileEnd)
		return;

	uint8_t buf[m_pieceLength];
	if (pieceBegin < f.info.begin) {
		size_t bufPos = f.info.begin - pieceBegin;
		if (fread(&buf[bufPos], 1, m_pieceLength - bufPos, fp) != m_pieceLength - bufPos)
			return;

		size_t index = f.info.index;
		while (bufPos != 0) {
			TorrentFile *cf = &m_files[--index];
			FILE *fpp = cf->fp;

			if (bufPos > cf->info.length) {
				if (fread(&buf[bufPos - cf->info.length], 1, bufPos, fpp) != cf->info.length)
					return;

				bufPos -= cf->info.length;
			} else {
				fseek(fpp, cf->info.length - bufPos, SEEK_SET);
				if (fread(&buf[0], 1, bufPos, fpp) != bufPos)
					return;

				break;
			}
		}

		if (is_write_eligible(pieceIndex, &buf[0], m_pieceLength))
			m_completedBits.set(pieceIndex);

		pieceBegin += pieceLength;
		++pieceIndex;
	}

	fseek(fp, pieceBegin - f.info.begin, SEEK_SET);
	size_t real = pieceIndex;
	bool escape = false;
#pragma omp parallel for
	for (size_t i = pieceIndex; i < m_pieces.size() - 1; ++i) {
		if (escape)
			continue;

		if (pieceBegin + m_pieceLength >= fileEnd) {
#pragma omp critical
			escape = true;
			real = i;
			continue;
		}

		fread(&buf[0], 1, m_pieceLength, fp);
		if (is_write_eligible(i, &buf[0], m_pieceLength))
			m_completedBits.set(i);

		pieceBegin += m_pieceLength;
	}

	pieceIndex = real;
	if (pieceIndex == total_pieces() - 1) {
		pieceLength = last_piece_length();
		fread(&buf[0], 1, pieceLength, fp);
		if (is_write_eligible(pieceIndex, &buf[0], pieceLength))
			m_completedBits.set(pieceIndex);
	}
}

void TorrentFileManagerImpl::scan_pieces()
{
	for (const TorrentFile &f : m_files)
		scan_file(f);
}

void TorrentFileManagerImpl::thread()
{
	std::unique_lock<std::mutex> lock(m_mutex, std::defer_lock);

	while (!m_stopped) {
		lock.lock();
		if (m_writeRequests.empty() && m_readRequests.empty())
			m_condition.wait(lock);

		if (m_stopped)
			break;

		// It doesn't really matter which one we process first
		// as torrent should be aware of our write process and should not
		// mark the piece as fully have before we fully wrote it to disk.
		while (!m_writeRequests.empty() && !m_stopped) {
			const WriteRequest &w = m_writeRequests.front();
			if (process_write(w))
				m_writeRequests.pop();
		}

		while (!m_readRequests.empty() && !m_stopped) {
			ReadRequest r = m_readRequests.top();
			if (process_read(r))
				m_readRequests.pop();	
		}

		lock.unlock();
	}
}

bool TorrentFileManagerImpl::process_read(const ReadRequest &r)
{
	const TorrentMeta *meta = m_torrent->meta();
	int64_t blockBegin = r.begin + r.index * meta->pieceLength();
	int64_t blockEnd = r.begin + r.size;
	uint8_t block[r.size];

	int64_t writePos = 0;
	for (const TorrentFile &f : m_files) {
		int64_t filePos = blockBegin + writePos;
		const TorrentFileInfo &i = f.info;
		if (filePos < i.begin)
			return false;

		int64_t fileEnd = i.begin + i.length;
		if (filePos > fileEnd)
			continue;
		fseek(f.fp, filePos - i.begin, SEEK_SET);

		size_t readSize = std::max(fileEnd - filePos, r.size - writePos);
		size_t maxRead = writePos + readSize;
		while (writePos < maxRead) {
			int read = fread(&block[writePos], 1, readSize - writePos, f.fp);
			if (read <= 0)
				return false;

			writePos += read;
		}
	}

	// TODO: schedule this as well, make sure "block" will live for the period
	m_torrent->onPieceReadComplete(r.from, r.index, r.begin, block, r.size);
	return true;
}

bool TorrentFileManagerImpl::process_write(const WriteRequest &w)
{
	const TorrentMeta *meta = m_torrent->meta();
	int64_t beginPos = w.index * meta->pieceLength();

	const uint8_t *buf = &w.data[0];
	size_t length = w.data.size();
	size_t off = 0;

	for (const TorrentFile &f : m_files) {
		const TorrentFileInfo &i = f.info;
		if (beginPos < i.begin)
			return false;

		int64_t fileEnd = i.begin + i.length;
		if (beginPos >= fileEnd)
			continue;

		int64_t amount = fileEnd - beginPos;
		if (amount > length)
			amount = length;

		fseek(f.fp, beginPos - i.begin, SEEK_SET);
		size_t wrote = fwrite(buf + off, 1, amount, f.fp);
		off += wrote;
		beginPos += wrote;
		length -= wrote;
	}

	m_completedBits.set(w.index);
	g_sched.addEvent(std::bind(&Torrent::onPieceWriteComplete, m_torrent, w.from, w.index), 0);
	return true;
}

size_t TorrentFileManagerImpl::get_next_piece(const std::function<bool (size_t)> &fun)
{
	size_t index = 0;
	int32_t priority = std::numeric_limits<int32_t>::max();

	std::lock_guard<std::mutex> guard(m_mutex);
	for (size_t i = 0; i < m_pieces.size(); ++i) {
		Piece *p = &m_pieces[i];
		if (m_completedBits.test(i) || !fun(i))
			continue;

		if (!p->pri) {
			p->pri = 1;
			return i;
		}

		if (priority > p->pri) {
			priority = p->pri;
			index = i;
		}
	}

	if (priority != std::numeric_limits<int32_t>::max()) {
		++m_pieces[index].pri;
		return index;
	}

	return std::numeric_limits<size_t>::max();
}

size_t TorrentFileManagerImpl::compute_downloaded()
{
	size_t i = 0;
	size_t downloaded = 0;
	size_t pieceLength = m_torrent->meta()->pieceLength();

	std::lock_guard<std::mutex> guard(m_mutex);
	for (; i < m_pieces.size() - 1; ++i)
		if (m_completedBits.test(i))
			downloaded += pieceLength;

	if (m_completedBits.test(i + 1))
		downloaded += last_piece_length();

	return downloaded;
}

TorrentFileManager::TorrentFileManager(Torrent *torrent)
{
	i = new TorrentFileManagerImpl(torrent);
}

TorrentFileManager::~TorrentFileManager()
{
	delete i;
}

const bitset *TorrentFileManager::completedBits() const
{
	return i->completed_bits();
}

size_t TorrentFileManager::pieceSize(size_t index) const
{
	return i->piece_length(index);
}

size_t TorrentFileManager::completedPieces() const
{
	return i->completed_pieces();
}

size_t TorrentFileManager::totalPieces() const
{
	return i->total_pieces();
}

size_t TorrentFileManager::getPieceforRequest(const std::function<bool (size_t)> &fun)
{
	return i->get_next_piece(fun);
}

size_t TorrentFileManager::computeDownloaded()
{
	return i->compute_downloaded();
}

bool TorrentFileManager::pieceDone(size_t index) const
{
	return i->piece_done(index);
}

bool TorrentFileManager::registerFiles(const std::string &baseDir, const TorrentFiles &files)
{
	// We have to initialize pieces here
	i->init_pieces();

	// Make sure the base directory exists
	MKDIR(baseDir);

	for (const TorrentFileInfo &inf : files) {
		std::string filePath = inf.path.substr(0, inf.path.find_last_of('/'));
		std::string fullPath = baseDir + inf.path;

		if (inf.path != filePath) {
			std::string path = baseDir + filePath;
			if (!validatePath(baseDir, path))
				return false;

			if (!nodeExists(path))
				MKDIR(path);
		}

		if (!nodeExists(fullPath)) {
			// touch
			FILE *fp = fopen(fullPath.c_str(), "wb");
			if (!fp)
				return false;

			fclose(fp);
		}

		// Open for read and write
		FILE *fp = fopen(fullPath.c_str(), "rb+");
		if (!fp)
			return false;

		TorrentFile f = {
			.fp = fp,
			.info = inf
		};
		i->push_file(f);
	}

	i->scan_pieces();
	return true;
}

bool TorrentFileManager::requestPieceBlock(size_t index, uint32_t from, int64_t begin, int64_t size)
{
	static uint32_t rid = 0;
	if (!i->intact(index) || !i->is_read_eligible(index, begin + size))
		return false;

	ReadRequest r = {
		.index = index,
		.id = rid++,
		.from = from,
		.begin = begin,
		.size = size
	};

	i->lock();
	i->push_read(r);
	i->unlock_and_notify();
	return true;
}

bool TorrentFileManager::writePieceBlock(size_t index, uint32_t from, DataBuffer<uint8_t> &&data)
{
	if (!i->intact(index) || !i->is_write_eligible(index, &data[0], data.size()))
		return false;

	WriteRequest w;
	w.index = index;
	w.from = from;
	w.data = std::move(data);

	i->lock();
	i->push_write(std::move(w));
	i->unlock_and_notify();
	return true;
}

