/*----------------------
 | opening_codec.cxx
 | Description: The fused decode-and-XOR. No engine headers: see opening_codec.h.
 | Author: suinevere
 | Dependencies: opening_codec.h
 ----------------------*/
#include "opening_codec.h"

/*----------------------
 | openingApplyDelta
 | Description: Decodes a page_rle stream and XORs the expanded bytes into page,
 |   which makes a keyframe and a delta the same operation against different
 |   predecessors.
 | Author: suinevere
 | Params: page -- pageLen bytes, modified in place; src -- encoded bytes;
 |         srcLen -- how many; pageLen -- exact number of bytes the stream must
 |         produce
 | Returns: true when exactly pageLen bytes were produced, false otherwise
 ----------------------*/
bool openingApplyDelta(uint8_t *page, const uint8_t *src, int32_t srcLen,
                       int32_t pageLen)
{
	int32_t si = 0;
	int32_t di = 0;

	while (si < srcLen) {
		const uint8_t ctl = src[si];
		const int32_t n = (int32_t)(ctl & 0x7F) + 1;
		si++;

		if (di + n > pageLen) {
			return false;
		}

		if ((ctl & 0x80) != 0) {
			if (si >= srcLen) {
				return false;
			}
			const uint8_t v = src[si];
			si++;
			for (int32_t k = 0; k < n; ++k) {
				page[di + k] = (uint8_t)(page[di + k] ^ v);
			}
		} else {
			if (si + n > srcLen) {
				return false;
			}
			for (int32_t k = 0; k < n; ++k) {
				page[di + k] = (uint8_t)(page[di + k] ^ src[si + k]);
			}
			si += n;
		}

		di += n;
	}

	return di == pageLen;
}
