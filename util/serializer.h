#ifndef __SERIALIZER_H
#define __SERIALIZER_H

#include <stdint.h>
#include <assert.h>

///////////////// Little Endian
inline uint16_t readLE16(const uint8_t *addr)
{
	return (uint16_t)addr[1] << 8 | addr[0];
}

inline uint32_t readLE32(const uint8_t *addr)
{
	return (uint32_t)readLE16(addr + 2) << 16 | readLE16(addr);
}

inline uint64_t readLE64(const uint8_t *addr)
{
	return (uint64_t)readLE32(addr + 4) << 32 | readLE32(addr);
}

inline void writeLE16(uint8_t *addr, uint16_t value)
{
	addr[1] = value >> 8;
	addr[0] = (uint8_t)value;
}

inline void writeLE32(uint8_t *addr, uint32_t value)
{
	writeLE16(addr + 2, value >> 16);
	writeLE16(addr, (uint16_t)value);
}

inline void writeLE64(uint8_t *addr, uint64_t value)
{
	writeLE32(addr + 4, value >> 32);
	writeLE32(addr, (uint32_t)value);
}

///////////////// Big Endian
inline uint16_t readBE16(const uint8_t *addr)
{
	return (uint16_t)addr[1] | (uint16_t)(addr[0]) << 8;
}

inline uint32_t readBE32(const uint8_t *addr)
{
	return (uint32_t)readBE16(addr + 2) | readBE16(addr) << 16;
}

inline uint64_t readBE64(const uint8_t *addr)
{
	return (uint64_t)readBE32(addr + 4) | (uint64_t)readBE32(addr) << 32;
}

inline void writeBE16(uint8_t *addr, uint16_t value)
{
	addr[0] = (uint8_t)value >> 8;
	addr[1] = (uint8_t)value;
}

inline void writeBE32(uint8_t *addr, uint32_t value)
{
	addr[0] = (uint8_t)(value >> 24);
	addr[1] = (uint8_t)(value >> 16);
	addr[2] = (uint8_t)(value >> 8);
	addr[3] = (uint8_t)(value);
}

inline void writeBE64(uint8_t *addr, uint64_t value)
{
	writeBE32(addr, value >> 32);
	writeBE32(addr + 4, (uint32_t)value);
}
#endif

