#include "util/serializer.h"
#include <stdio.h>
#include <assert.h>

#include <sstream>

int main() {
	uint8_t data[4] = { 0, 0, 0, 14 };
	printf("big: %ld little: %ld\n", readBE32(data), readLE32(data));
	printf("big: %hd little: %hd\n\n", readBE16(data), readLE16(data));
	assert(readBE32(data) == 14 && readBE16(data) == 0);

	uint8_t data0[4] = { 14, 0, 0, 0 };
	printf("little: %ld big: %ld\n", readLE32(data0), readBE32(data0));
	printf("little: %hd big: %hd\n\n", readLE16(data0), readBE16(data0));
	assert(readLE16(data0) == 14);

	uint32_t i = 0;
	i |= (uint32_t)data0[3] << 24;
	i |= (uint32_t)data0[2] << 16;
	i |= (uint32_t)data0[1] << 8;
	i |= (uint32_t)data0[0];
	printf("little: %d\n", i);

	i = 0;
	i |= (uint32_t)data[3];
	i |= (uint32_t)data[2] << 8;
	i |= (uint32_t)data[1] << 16;
	i |= (uint32_t)data[0] << 24;
	printf("big: %d\n\n", i);

	return 0;
}

