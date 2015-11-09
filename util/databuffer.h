#ifndef __DATABUFFER_H
#define __DATABUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>

template <typename T>
class DataBuffer
{
public:
	DataBuffer(size_t res = 64) :
		m_size(0),
		m_capacity(res),
		m_buffer(new T[m_capacity])
	{
	}
	~DataBuffer() { delete[] m_buffer; }

	inline size_t size() const { return m_size; }
	inline size_t cap() const { return m_capacity; }
	inline T *data() const { return m_buffer; }

	inline const T &operator[](size_t i) const
	{
		assert(i < m_capacity);
		return m_buffer[i];
	}
	inline T &operator[](size_t i)
	{
		assert(i < m_capacity);
		return m_buffer[i];
	}

	inline void setData(T *data, size_t size)
	{
		delete []m_buffer;
		m_buffer = new T[size];
		m_capacity = size;
		m_size = 0;
		for (size_t i = 0; i < size; ++i)
			m_buffer[i] = data[i];
	}
	inline void setSize(size_t s) { m_size = s; }

	inline void clear(void)
	{
		memset(&m_buffer[0], 0x00, m_capacity);
		m_size = 0;
	}

	inline void reserve(size_t n)
	{
		if (n > m_capacity) {
			T *buffer = new T[n];
			for (size_t i=0; i<m_size; ++i)
				buffer[i] = m_buffer[i];
			delete[] m_buffer;
			m_buffer = buffer;
			m_capacity = n;
		}
	}

	inline void grow(size_t n)
	{
		if (n <= m_size)
			return;

		if (n > m_capacity) {
			size_t newcapacity = m_capacity;
			do
				newcapacity *= 2;
			while (newcapacity < n);
			reserve(newcapacity);
		}
		m_size = n;
	}

	inline void add(const T& v)
	{
		grow(m_size + 1);
		m_buffer[m_size-1] = v;
	}

	inline DataBuffer &operator<<(const T &t)
	{
		add(t);
		return *this;
	}

private:
	size_t m_size;
	size_t m_capacity;
	T *m_buffer;
};

#endif

