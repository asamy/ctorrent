#include <stdio.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include <iostream>
#include <vector>

int main()
{
	uint8_t payload[] = {
		80,	/* 0101 0000 => 2 pieces (indices 1 and 3) */
		120,	/* 0111 1000 => 4 pieces (indices 9, 10, 11, and 12) */
		160,	/* 1010 0000 => 2 pieces (indices 16 and 18)  */
		255,	/* 1111 1111 => 8 pieces (indices 24, 25, 26, 27, 28, 29, 30, and 31 */
		7,	/* 0000 0111 => 3 pieces (indices 37, 38 and 39  */
		15,	/* 0000 1111 => 4 pieces (indices 45, 46, 47 and 48  */
	};
	size_t payloadSize = sizeof(payload);

	std::vector<size_t> pieces;
	for (size_t i = 0, index = 0; i < payloadSize; ++i) {	
		uint8_t b = payload[i];
		if (b == 0)
			continue;

		uint32_t leading = CHAR_BIT - (sizeof(uint32_t) * CHAR_BIT - __builtin_clz(b));
		uint32_t trailing = __builtin_ctz(b);

		// skip leading zero bits first, we skip trailing zero bits later
		index += leading;

		// push this piece, we know it's there
		pieces.push_back(index++);

		b >>= trailing + leading;
		for (; b != 0; b >>= 1, ++index)
			if (b & 1) {
				printf("%d: %d %d\n", i, b, index);
				pieces.push_back(index);
			}

		// skip trailing
		index += trailing;
	}

	printf("total pieces found: %zd\n", pieces.size());
	for (size_t p : pieces)
		printf("%zd\n", p);
}

