/*
 * Copyright (c) 2013-2014 Ahmed Samy  <f.fallen45@gmail.com>
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
#ifndef __OUTPUTMESSAGE_H
#define __OUTPUTMESSAGE_H

#include "decl.h"

class OutputMessage
{
public:
	explicit OutputMessage(DataBuffer<uint8_t> &&buffer)
		: m_buffer(std::move(buffer)),
		  m_pos(buffer.size()),
		  m_order(ByteOrder::BigEndian)
	{
	}
	OutputMessage(ByteOrder order, size_t fixedSize = 0);
	~OutputMessage();

	void clear() { m_buffer.clear(); m_pos = 0; }
	void addByte(uint8_t byte);
	void addBytes(const uint8_t *bytes, size_t size);
	void addU16(uint16_t val);
	void addU32(uint32_t val);
	void addU64(uint64_t val);
	void addString(const std::string &str);

	const uint8_t *data() const { return &m_buffer[m_pos]; }
	const uint8_t *data(size_t p) const { return &m_buffer[p]; }
	size_t size() const { return m_pos; }

	inline const uint8_t &operator[] (size_t index) const { return m_buffer[index]; }
	inline OutputMessage &operator<<(const uint8_t &b) { addByte(b); return *this; }
	inline OutputMessage &operator<<(const uint16_t &u) { addU16(u); return *this; }
	inline OutputMessage &operator<<(const uint32_t &u) { addU32(u); return *this; }
	inline OutputMessage &operator<<(const uint64_t &u) { addU64(u); return *this; }
	inline OutputMessage &operator<<(const std::string &s) { addString(s); return *this; }

private:
	DataBuffer<uint8_t> m_buffer;
	size_t m_pos;
	ByteOrder m_order;
};

#endif

