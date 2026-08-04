/*----------------------
 | opening_codec.h
 | Description: Applies one run-length coded XOR delta to a 4bpp page, which is
 |   how the title opening streams 398 frames without ever holding two at once.
 | Author: suinevere
 | Dependencies: stdint.h
 ----------------------*/
#ifndef OPENING_CODEC_H
#define OPENING_CODEC_H

#include <stdint.h>

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
                       int32_t pageLen);

#endif /* OPENING_CODEC_H */
