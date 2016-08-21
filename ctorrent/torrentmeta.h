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
#ifndef __TORRENTMETA_H
#define __TORRENTMETA_H

#include "torrentfilemanager.h"

#include <bencode/bencode.h>

struct sha1sum {
	uint32_t i[5];
	operator uint32_t *() { return &i[0]; }
};

class TorrentMeta {
public:
	TorrentMeta();
	~TorrentMeta();

	bool parse(const std::string &fileName);
	bool parse(const char *data, size_t size);

	inline TorrentFiles files() const { return m_files; }
	inline std::string baseDir() const { return m_dirName; }
	inline std::vector<sha1sum> sha1sums() const { return m_sha1sums; }

	inline std::string name() const { return m_name; }
	inline std::string comment() const { return m_comment; }
	inline std::string createdBy() const { return m_createdBy; }
	inline std::string tracker() const { return m_mainTracker; }
	inline VectorType trackers() const { return m_trackers; }

	inline const uint32_t *checkSum() const { return &m_checkSum[0]; }
	inline size_t pieceLength() const { return m_pieceLength; }
	inline size_t totalSize() const { return m_totalSize; }

protected:
	bool internalParse(Dictionary &d, Bencode &b);
	bool parseFile(const VectorType &pathList, size_t &index, size_t &begin, size_t length);

private:
	std::string m_dirName;
	std::string m_name;
	std::string m_comment;
	std::string m_createdBy;
	std::string m_mainTracker;

	uint32_t m_checkSum[5];
	size_t m_pieceLength;
	size_t m_totalSize;

	TorrentFiles m_files;
	VectorType m_trackers;
	std::vector<sha1sum> m_sha1sums;
};

#endif

