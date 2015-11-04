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
#include "bencode.h"

#include <sstream>
#include <fstream>
#include <iostream>

Dictionary Bencode::decode(const char *data, size_t size)
{
	if (data[0] != 'd')
		return Dictionary();

	++m_pos;
	m_buffer = const_cast<char *>(data);
	m_buffer.setSize(size);
	return readDictionary();
}

Dictionary Bencode::decode(const std::string& fileName)
{
	std::ifstream f(fileName, std::ios_base::binary | std::ios_base::in);
	if (!f.is_open())
		return Dictionary();

	f.seekg(0, f.end);
	size_t size = f.tellg();
	f.seekg(0, f.beg);

	char *buffer = new char[size];
	f.read(buffer, size);
	f.close();
	return decode(buffer, size);
}

template <typename T>
bool Bencode::readIntUntil(const char byte, T &value)
{
	// Find first occurance of byte in the data buffer
	size_t size = 0, pos = 0;
	for (pos = m_pos; pos < m_buffer.size() && m_buffer[pos] != byte; ++pos, ++size);
	// Sanity check
	if (m_buffer[pos] != byte)
		return false;

	char buffer[size];
	memcpy(buffer, &m_buffer[m_pos], size);
	buffer[size] = '\0';

	std::istringstream ss(buffer);
	ss >> value;

	m_pos = pos + 1;
	return true;
}

int64_t Bencode::readInt()
{
	int64_t i;
	if (readIntUntil('e', i))
		return i;

	return ~0;
}

uint64_t Bencode::readUint()
{
	uint64_t u;
	if (readIntUntil('e', u))
		return u;

	return std::numeric_limits<uint64_t>::max();
}

std::vector<boost::any> Bencode::readVector()
{
	std::vector<boost::any> ret;

	for (;;) {
		char byte;
		if (!unpackByte(byte))
			return ret;

		switch (byte) {
		case 'i':		ret.push_back(readInt());	 break;
		case 'l':		ret.push_back(readVector());	 break;
		case 'd':		ret.push_back(readDictionary()); break;
		case 'e':		return ret;
		default:
			--m_pos;
			ret.push_back(readString());
			break;
		}
	}

	return ret;
}

std::string Bencode::readString()
{
	uint64_t len;
	if (!readIntUntil(':', len))
		return std::string();

	if (len + m_pos > m_buffer.size())
		return std::string();

	char buffer[len];
	memcpy(buffer, (char *)&m_buffer[m_pos], len);

	m_pos += len;
	return std::string((const char *)buffer, len);
}

Dictionary Bencode::readDictionary()
{
	Dictionary ret;

	for (;;) {
		std::string key = readString();
		if (key.empty())
			return Dictionary();

		char byte;
		if (!unpackByte(byte))
			return Dictionary();

		switch (byte) {
		case 'i':		ret[key] = readInt();		break;
		case 'l':		ret[key] = readVector();	break;
		case 'd':		ret[key] = readDictionary();	break;
		default:
			{
				--m_pos;
				ret[key] = readString();
				break;
			}
		}

		if (!unpackByte(byte))
			return Dictionary();
		else if (byte == 'e')
			break;
		else
			--m_pos;
	}

	return ret;
}
