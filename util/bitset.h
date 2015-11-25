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
#ifndef __BITSET_H
#define __BITSET_H

#include <stddef.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

// TODO: Perhaps make this templated so that the user can specify type?
// TODO: Remove rounded size and make count() compatible with the latter
class bitset {
public:
	bitset(size_t size)
	{
		construct(size);
	}
	bitset()
	{
		m_size = 0;
		m_bits = nullptr;
	}
	bitset(bitset const &) = delete;
	~bitset() { delete m_bits; m_bits = nullptr; }

	void construct(size_t size)
	{
		m_size = size;
		m_roundedSize = ((size + 32 - 1) / 32) * 32;
		m_bits = new uint8_t[m_roundedSize];
		memset(m_bits, 0x00, m_roundedSize);
	}

	// we fold i down to be in range from 0 to 7
	bool test(size_t i) const { return !!(bitsAt(i) & (1 << (i % CHAR_BIT))); }
	void set(size_t i) { bitsAt(i) |= (1 << (i % CHAR_BIT)); }
	void set(size_t i, bool v) { bitsAt(i) ^= ((int)-v ^ bitsAt(i)) & (1 << (i % CHAR_BIT)); }
	void clear(size_t i) { bitsAt(i) &= ~(1 << (i % CHAR_BIT)); }
	void toggle(size_t i) { bitsAt(i) ^= (1 << (i % CHAR_BIT)); }

	bool operator[] (size_t i) { return test(i); }
	bool operator[] (size_t i) const { return test(i); }

	const uint8_t *bits() const { return m_bits; }
	uint8_t *bits() { return m_bits; }

	size_t roundedSize() const { return m_roundedSize; }
	size_t size() const { return m_size; }
	size_t count() const
	{
		// Hamming Weight algorithm
		size_t set = 0;
		const uint8_t *src = m_bits;
		const uint8_t *dst = m_bits + m_roundedSize;
		while (src < dst) {
			uint32_t v = *(uint32_t *)src;
			v = v - ((v >> 1) & 0x55555555);
			v = (v & 0x33333333) + ((v >> 2) & 0x33333333);
			set += (((v + (v >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;
			src += 4;
		}

		return set;
	}

	// Each uint8_t has 8 bits so we fold it down to figure out where this bit is at.
	// Could use a bigger type but it really doesn't matter anyway...
	uint8_t bitsAt(int i) const { return m_bits[i / CHAR_BIT]; }
	uint8_t &bitsAt(int i) { return m_bits[i / CHAR_BIT]; }

private:
	uint8_t *m_bits;
	size_t m_size;
	size_t m_roundedSize;
};

#endif

