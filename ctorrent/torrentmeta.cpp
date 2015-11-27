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
#include "torrentmeta.h"

#include <util/auxiliar.h>
#include <boost/uuid/sha1.hpp>

TorrentMeta::TorrentMeta()
	: m_pieceLength(0),
	  m_totalSize(0)
{
}

TorrentMeta::~TorrentMeta()
{
}

bool TorrentMeta::parse(const std::string &fileName)
{
	Bencode bencode;
	Dictionary dict = bencode.decode(fileName);

	if (dict.empty())
		return false;

	return internalParse(dict, bencode);
}

bool TorrentMeta::parse(const char *data, size_t size)
{
	Bencode bencode;
	Dictionary dict = bencode.decode(data, size);

	if (dict.empty())
		return false;

	return internalParse(dict, bencode);
}

bool TorrentMeta::internalParse(Dictionary &dict, Bencode &bencode)
{
	if (!dict.count("announce"))
		return false;

	m_mainTracker = Bencode::cast<std::string>(dict["announce"]);
	if (dict.count("comment"))
		m_comment = Bencode::cast<std::string>(dict["comment"]);
	if (dict.count("created by"))
		m_createdBy = Bencode::cast<std::string>(dict["created by"]);
	if (dict.count("announce-list"))
		m_trackers = Bencode::cast<VectorType>(dict["announce-list"]);

	Dictionary info = Bencode::cast<Dictionary>(dict["info"]);
	if (info.empty())
		return false;

	size_t pos = bencode.pos();
	bencode.encode(info);
	if (!info.count("pieces") || !info.count("piece length"))
		return false;

	size_t bufferSize;
	const char *buffer = bencode.buffer(pos, bufferSize);

	boost::uuids::detail::sha1 sha1;
	sha1.process_bytes(buffer, bufferSize);

	uint32_t digest[5];
	sha1.get_digest(digest);
	for (int i = 0; i < 5; ++i)
		writeBE32(&m_checkSum[i * 4], digest[i]);	

	m_name = Bencode::cast<std::string>(info["name"]);
	m_pieceLength = Bencode::cast<int64_t>(info["piece length"]);

	std::string pieces = Bencode::cast<std::string>(info["pieces"]);
	m_sha1sums.reserve(pieces.size() / 20);

	for (size_t i = 0; i < pieces.size(); i += 20)
		m_sha1sums.push_back(std::string(pieces.c_str() + i, 20));

	if (info.count("files")) {
		m_dirName = Bencode::cast<std::string>(info["name"]);

		size_t index = 0;
		int64_t begin = 0;

		const boost::any &any = info["files"];
		if (any.type() == typeid(Dictionary)) {
			for (const auto &pair : Bencode::cast<Dictionary>(any)) {
				Dictionary v = Bencode::cast<Dictionary>(pair.second);
				VectorType pathList = Bencode::cast<VectorType>(v["path"]);
	
				if (!parseFile(pathList, index, begin, Bencode::cast<int64_t>(v["length"])))
					return false;
			}
		} else if (any.type() == typeid(VectorType)) {
			for (const auto &f : Bencode::cast<VectorType>(any)) {
				Dictionary v = Bencode::cast<Dictionary>(f);
				VectorType pathList = Bencode::cast<VectorType>(v["path"]);

				if (!parseFile(pathList, index, begin, Bencode::cast<int64_t>(v["length"])))
					return false;
			}
		} else {
			// ... nope
			return false;
		}
	} else {
		int64_t length = Bencode::cast<int64_t>(info["length"]);
		if (length <= 0)
			return false;

		TorrentFileInfo f = {
			.path = m_name,
			.index = 0,
			.begin = 0,
			.length = length
		};

		m_totalSize = length;
		m_files.push_back(f);
	}

	return true;
}

bool TorrentMeta::parseFile(const VectorType &pathList, size_t &index, int64_t &begin, int64_t length)
{
	std::string path = "";
	for (const boost::any &any : pathList) {
		const std::string &s = Bencode::cast<std::string>(any);
		if (!path.empty())
			path += PATH_SEP;
		path += s;
	}

	TorrentFileInfo file = {
		.path = path,
		.index = index,
		.begin = begin,
		.length = length
	};

	m_files.push_back(file);
	m_totalSize += length;
	begin += length;
	++index;
	return true;
}

