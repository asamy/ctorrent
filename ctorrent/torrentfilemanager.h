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
#ifndef __TORRENTFILEMANAGER_H
#define __TORRENTFILEMANAGER_H

#include <util/databuffer.h>
#include <util/bitset.h>

#include <string>
#include <vector>
#include <cstdint>
#include <functional>

struct TorrentFileInfo {
	std::string path;
	size_t index;
	int64_t begin;
	int64_t length;
};
typedef std::vector<TorrentFileInfo> TorrentFiles;

class Torrent;
class TorrentFileManagerImpl;
class TorrentFileManager {
public:
	TorrentFileManager(Torrent *torrent);
	~TorrentFileManager();

	const bitset *completedBits() const;
	size_t pieceSize(size_t index) const;
	size_t completedPieces() const;
	size_t totalPieces() const;
	size_t getPieceforRequest(const std::function<bool (size_t)> &fun);
	size_t computeDownloaded();

	bool pieceDone(size_t index) const;
	bool registerFiles(const std::string &baseDir, const TorrentFiles &files);
	bool requestPieceBlock(size_t index, uint32_t from, int64_t begin, int64_t size);
	bool writePieceBlock(size_t index, uint32_t from, DataBuffer<uint8_t> &&data);

private:
	TorrentFileManagerImpl *i;
};

#endif

