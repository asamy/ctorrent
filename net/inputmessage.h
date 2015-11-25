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
#ifndef __INPUTMESSAGE_H
#define __INPUTMESSAGE_H

#include "decl.h"

class InputMessage
{
public:
	InputMessage(uint8_t *data, size_t size, ByteOrder order);
	InputMessage(ByteOrder order);
	~InputMessage();

	void setData(uint8_t *d) { m_data = d; }
	size_t getSize() { return m_size; }
	void setSize(size_t size) { m_size = size; }
	void setByteOrder(ByteOrder order) { m_order = order; }

	uint8_t *getBuffer(size_t size);
	uint8_t *getBuffer(void);
	uint8_t getByte();
	uint16_t getU16();
	uint32_t getU32();
	uint64_t getU64();
	std::string getString();

	inline uint8_t &operator[] (size_t i) { return m_data[i]; }
	inline InputMessage &operator=(uint8_t *data) { m_data = data; return *this; }
	inline InputMessage &operator>>(uint8_t &b) { b = getByte(); return *this; }
	inline InputMessage &operator>>(uint16_t &u) { u = getU16(); return *this; }
	inline InputMessage &operator>>(uint32_t &u) { u = getU32(); return *this; }
	inline InputMessage &operator>>(uint64_t &u) { u = getU64(); return *this; }
	inline InputMessage &operator>>(std::string &s) { s = getString(); return *this; }

private:
	uint8_t *m_data;
	size_t m_size;
	uint32_t m_pos;
	ByteOrder m_order;
};

#endif

