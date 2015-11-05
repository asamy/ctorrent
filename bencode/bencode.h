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
#ifndef __BENCODE_H
#define __BENCODE_H

#include <boost/any.hpp>

#include <string>
#include <map>
#include <vector>

#include <util/serializer.h>
#include <util/databuffer.h>

typedef std::map<std::string, boost::any> Dictionary;
typedef std::vector<boost::any> VectorType;

class Bencode {
public:
	Bencode() { m_pos = 0; }
	~Bencode() = default;

	Dictionary decode(const std::string& fileName);
	Dictionary decode(const char *, size_t);
	inline void encode(const Dictionary& dict) { return writeDictionary(dict); }

	template <typename T>
	static inline T cast(const boost::any &value)
	{
		return boost::any_cast<T>(value);
	}
	template <typename T>
	static inline T unsafe_cast(const boost::any &value)
	{
		return *boost::unsafe_any_cast<T>(&value);
	}

	size_t pos() const { return m_pos; }
	const char *buffer(size_t pos, size_t &bufferSize) const {
		bufferSize = m_buffer.size() - pos;
		return &m_buffer[pos];
	}

protected:
	inline int64_t readInt();
	inline uint64_t readUint();
	VectorType readVector();
	std::string readString();
	Dictionary readDictionary();

	void writeString(const std::string &);
	void writeInt(int64_t);
	void writeUint(uint64_t);
	void writeVector(const VectorType &);
	void writeDictionary(const Dictionary &);
	void writeType(const boost::any *);

	template <typename T>
	bool readIntUntil(const char byte, T &value);

	inline bool unpackByte(char &b) {
		if (m_pos + 1 > m_buffer.size())
			return false;

		b = m_buffer[m_pos++];
		return true;
	}

	void internalWriteString(const std::string&);

private:
	DataBuffer<char> m_buffer;
	size_t m_pos;
};

#endif
