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
#ifdef _MSC_VER
#include <intrin.h>
#endif

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
		m_bits = new uint8_t[m_size];
		memset(m_bits, 0x00, m_size);
	}
	void resize(size_t size)
	{
		uint8_t *bits = new uint8_t[size];
		if (!bits)
			return;

		memcpy(bits, m_bits, size);
		free(m_bits);

		m_bits = bits;
		m_size = size;
	}

	// simply modulus by 8 but since this is a bitset lets keep this all
	// bit relevant
	bool test(size_t i) const { return !!(bitsAt(i) & (1 << (i & 7))); }
	void set(size_t i) { bitsAt(i) |= (1 << (i & 7)); }
	void set(size_t i, bool v) { bitsAt(i) ^= ((int)-v ^ bitsAt(i)) & (1 << (i & 7)); }
	void clear(size_t i) { bitsAt(i) &= ~(1 << (i & 7)); }
	void toggle(size_t i) { bitsAt(i) ^= (1 << (i & 7)); }

	bool operator[] (size_t i) { return test(i); }
	bool operator[] (size_t i) const { return test(i); }

	const uint8_t *bits() const { return m_bits; }
	uint8_t *bits() { return m_bits; }

	size_t popcnt(uint64_t v) const
	{
#ifdef __GNUC__
		return __builtin_popcount(v);
#elif _MSC_VER
		return __popcnt64(v);
#else
		// Hamming Weight
		v = v - ((v >> 1) & 0x5555555555555555);
		v = (v & 0x3333333333333333) + ((v >> 2) & 0x3333333333333333);
		return (((v + (v >> 4)) & 0x0F0F0F0F0F0F0F0F) * 0x0101010101010101) >> 56;
#endif
	}

	size_t size() const { return m_size; }
	size_t count() const
	{
		size_t set = 0;
		const uint8_t *src = m_bits;
		const uint8_t *dst = m_bits + m_size;
		while (src + 7 <= dst) {
			set += popcnt(*(uint64_t *)src);
			src += 4;
		}

		if (src + 3 <= dst) {
			set += popcnt(*(uint32_t *)src);
			src += 4;
		}

		if (src + 1 < dst) {
			set += popcnt(*(uint16_t *)src);
			src += 2;
		}

		if (src < dst)
			set += popcnt(*(uint8_t *)src);

		return set;
	}

	// Simply division by 8 but since this is a bitset let's keep it all
	// bit relevant
	uint8_t bitsAt(int i) const { return m_bits[i >> 3]; }
	uint8_t &bitsAt(int i) { return m_bits[i >> 3]; }

	void raw_set(const uint8_t *bits, size_t size)
	{
		m_size = size;
		memcpy(&m_bits[0], bits, size);
	}

private:
	uint8_t *m_bits;
	size_t m_size;
};

#endif

