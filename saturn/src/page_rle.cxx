/*----------------------
 | page_rle.cxx
 | Description: The run-length codec described in page_rle.h. Deliberately free
 |   of engine headers so the whole of it runs under the host test suite.
 | Author: suinevere
 | Dependencies: page_rle.h
 ----------------------*/
#include "page_rle.h"

/*----------------------
 | PAGE_RLE_MAX_BLOCK
 | Description: Longest run or literal block one control byte can describe.
 |   Seven bits of count, stored biased by one so a zero-length block cannot be
 |   encoded and every control byte makes progress.
 | Author: suinevere
 ----------------------*/
enum {
	PAGE_RLE_MAX_BLOCK = 128
};

/*----------------------
 | pageRleRunLength
 | Description: How many times the byte at src repeats, capped at the longest
 |   block a control byte can describe.
 | Author: suinevere
 | Params: src -- start of the candidate run; remaining -- bytes left in the
 |   input
 | Returns: the run length, at least 1
 ----------------------*/
static int32_t pageRleRunLength(const uint8_t *src, int32_t remaining) {
	int32_t n = 1;
	while (n < remaining && n < PAGE_RLE_MAX_BLOCK && src[n] == src[0]) {
		++n;
	}
	return n;
}

int32_t pageRleEncode(const uint8_t *src, int32_t srcLen, uint8_t *dst,
                      int32_t dstCap) {
	if (src == 0 || dst == 0 || srcLen < 0 || dstCap < 0) {
		return -1;
	}

	int32_t in = 0;
	int32_t out = 0;

	while (in < srcLen) {
		const int32_t run = pageRleRunLength(src + in, srcLen - in);

		if (run >= 2) {
			if (out + 2 > dstCap) {
				return -1;
			}
			dst[out++] = (uint8_t)(0x80 | (run - 1));
			dst[out++] = src[in];
			in += run;
			continue;
		}

		int32_t lit = 0;
		while (in + lit < srcLen && lit < PAGE_RLE_MAX_BLOCK &&
		       pageRleRunLength(src + in + lit, srcLen - in - lit) < 2) {
			++lit;
		}

		if (out + 1 + lit > dstCap) {
			return -1;
		}
		dst[out++] = (uint8_t)(lit - 1);
		for (int32_t i = 0; i < lit; ++i) {
			dst[out++] = src[in + i];
		}
		in += lit;
	}

	return out;
}

bool pageRleDecode(const uint8_t *src, int32_t srcLen, uint8_t *dst,
                   int32_t dstLen) {
	if (src == 0 || dst == 0 || srcLen < 0 || dstLen < 0) {
		return false;
	}

	int32_t in = 0;
	int32_t out = 0;

	while (in < srcLen) {
		const uint8_t control = src[in++];
		const int32_t count = (int32_t)(control & 0x7F) + 1;

		if (control & 0x80) {
			if (in >= srcLen || out + count > dstLen) {
				return false;
			}
			const uint8_t value = src[in++];
			for (int32_t i = 0; i < count; ++i) {
				dst[out++] = value;
			}
		} else {
			if (in + count > srcLen || out + count > dstLen) {
				return false;
			}
			for (int32_t i = 0; i < count; ++i) {
				dst[out++] = src[in++];
			}
		}
	}

	return out == dstLen;
}
