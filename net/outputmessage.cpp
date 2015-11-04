/*
 * Copyright (c) 2013, 2014 Ahmed Samy  <f.fallen45@gmail.com>
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
#include "outputmessage.h"

#include <string.h>

#include <util/serializer.h>

OutputMessage::OutputMessage(ByteOrder order, size_t fixedSize)
	: m_pos(0),
	  m_order(order)
{
	if (fixedSize != 0)
		m_buffer.reserve(fixedSize);
}

OutputMessage::~OutputMessage()
{

}

void OutputMessage::addByte(uint8_t byte)
{
	m_buffer[m_pos++] = byte;
}

void OutputMessage::addU16(uint16_t val)
{
	if (m_order == ByteOrder::BigEndian)
		writeBE16(&m_buffer[m_pos], val);
	else
		writeLE16(&m_buffer[m_pos], val);
	m_pos += 2;
}

void OutputMessage::addU32(uint32_t val)
{
	if (m_order == ByteOrder::BigEndian)
		writeBE32(&m_buffer[m_pos], val);
	else
		writeLE32(&m_buffer[m_pos], val);
	m_pos += 4;
}

void OutputMessage::addU64(uint64_t val)
{
	if (m_order == ByteOrder::BigEndian)
		writeBE64(&m_buffer[m_pos], val);
	else
		writeLE64(&m_buffer[m_pos], val);
	m_pos += 8;
}

void OutputMessage::addString(const std::string &str)
{
	uint16_t len = str.length();

	addU16(len);
	memcpy((char *)&m_buffer[m_pos], str.c_str(), len);
	m_pos += len;
}
