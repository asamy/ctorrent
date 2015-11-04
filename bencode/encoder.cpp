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
#include <util/auxiliar.h>

#include "bencode.h"

void Bencode::internalWriteString(const std::string& str)
{
	for (size_t i = 0; i < str.length(); ++i)
		m_buffer.add(str[i]);
	m_pos += str.length();
}

void Bencode::writeString(const std::string& str)
{
	internalWriteString(std::to_string(str.length()));
	m_buffer.add(':');
	++m_pos;
	internalWriteString(str);
}

void Bencode::writeInt(int64_t i)
{
	m_buffer.add('i');
	++m_pos;
	internalWriteString(std::to_string(i));
	m_buffer.add('e');
	++m_pos;
}

void Bencode::writeUint(uint64_t i)
{
	m_buffer.add('i');
	++m_pos;
	internalWriteString(std::to_string(i));
	m_buffer.add('e');
	++m_pos;
}

void Bencode::writeVector(const VectorType& vector)
{
	m_buffer.add('l');
	++m_pos;
	for (const boost::any& value : vector)
		writeType(&value);
	m_buffer.add('e');
	++m_pos;
}

void Bencode::writeDictionary(const Dictionary& map)
{
	m_buffer.add('d');
	++m_pos;

	for (const auto& pair : map) {
		writeString(pair.first);
		writeType(&pair.second);
	}

	m_buffer.add('e');
	++m_pos;
}

void Bencode::writeType(const boost::any *value)
{
	const std::type_info& type = value->type();

	if (type == typeid(int64_t))
		writeInt(*boost::unsafe_any_cast<int64_t>(value));
	else if (type == typeid(uint64_t))
		writeUint(*boost::unsafe_any_cast<uint64_t>(value));
	else if (type == typeid(int) || type == typeid(int32_t))
		writeInt((int64_t)*boost::unsafe_any_cast<int>(value));
	else if (type == typeid(uint32_t))
		writeUint((uint64_t)*boost::unsafe_any_cast<uint32_t>(value));
	else if (type == typeid(std::string))
		writeString(*boost::unsafe_any_cast<std::string>(value));
	else if (type == typeid(const char *))
		writeString(*boost::unsafe_any_cast<const char *>(value));
	else if (type == typeid(Dictionary))
		writeDictionary(*boost::unsafe_any_cast<Dictionary>(value));
	else if (type == typeid(VectorType))
		writeVector(*boost::unsafe_any_cast<VectorType>(value));
}
