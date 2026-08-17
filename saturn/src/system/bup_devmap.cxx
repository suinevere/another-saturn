/*----------------------
 | bup_devmap.cxx
 | Description: The responding-index search behind bup_devmap.h.
 | Author: suinevere
 | Dependencies: bup_devmap.h
 ----------------------*/
#include "bup_devmap.h"

void bupDevmapResolve(const int *present, int count,
                      int *internalIdx, int *cartIdx)
{
	int first = BUP_DEVMAP_NONE;
	int second = BUP_DEVMAP_NONE;

	for (int i = 0; i < count; ++i) {
		if (!present[i]) {
			continue;
		}
		if (first == BUP_DEVMAP_NONE) {
			first = i;
		} else if (second == BUP_DEVMAP_NONE) {
			second = i;
		}
	}

	*internalIdx = (first != BUP_DEVMAP_NONE) ? first : 0;
	*cartIdx = second;
}
