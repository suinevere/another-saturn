/*----------------------
 | page_rle.h
 | Description: Run-length coding for one 4bpp video page, so a save can carry
 |   the frame the player was looking at without carrying all 128 KB of pages.
 |   Another World's art is flat polygon fill, which is why a byte-wise RLE is
 |   worth the trouble here at all. No engine dependency, so the codec is
 |   host-testable the same way menu_draw.cxx is.
 | Author: suinevere
 | Dependencies: stdint.h
 ----------------------*/
#ifndef PAGE_RLE_H
#define PAGE_RLE_H

#include <stdint.h>

/*----------------------
 | pageRleEncode
 | Description: Encodes src as alternating literal and run blocks. A control
 |   byte with the high bit set introduces a run of (c & 0x7F) + 1 copies of the
 |   next byte; otherwise it introduces (c & 0x7F) + 1 literal bytes. Refuses
 |   rather than truncating when the result would not fit, since a truncated
 |   save is worse than a refused one.
 | Author: suinevere
 | Params: src -- bytes to encode; srcLen -- how many; dst -- destination;
 |   dstCap -- destination capacity
 | Returns: encoded length, or -1 if it does not fit in dstCap
 ----------------------*/
int32_t pageRleEncode(const uint8_t *src, int32_t srcLen, uint8_t *dst,
                      int32_t dstCap);

/*----------------------
 | pageDeltaEncode
 | Description: Encodes src in the same block format as pageRleEncode, but over
 |   the difference between each byte and the byte one row earlier. A page of
 |   flat polygon fills repeats scanlines, and differencing turns those repeats
 |   into runs of zero, which is what buys the save enough room to keep a
 |   background at all.
 | Author: suinevere
 | Params: src -- bytes to encode; srcLen -- how many; stride -- bytes per row;
 |   rowStep -- keep one scanline in this many, 1 for all of them; dst --
 |   destination; dstCap -- destination capacity
 | Returns: encoded length, or -1 if it does not fit or the arguments are bad
 ----------------------*/
int32_t pageDeltaEncode(const uint8_t *src, int32_t srcLen, int32_t stride,
                        int32_t rowStep, uint8_t *dst, int32_t dstCap);

/*----------------------
 | pageRleDecode
 | Description: Reverses pageRleEncode. Rejects any stream that would run past
 |   either end rather than writing what it can.
 | Author: suinevere
 | Params: src -- encoded bytes; srcLen -- how many; dst -- destination;
 |   dstLen -- exact number of bytes the stream must produce
 | Returns: true when exactly dstLen bytes were produced, false otherwise
 ----------------------*/
bool pageRleDecode(const uint8_t *src, int32_t srcLen, uint8_t *dst,
                   int32_t dstLen);

/*----------------------
 | pageDeltaDecode
 | Description: Reverses pageDeltaEncode, undoing the row differencing in place
 |   after the blocks are expanded. Rejects whatever pageRleDecode rejects.
 | Author: suinevere
 | Params: src -- encoded bytes; srcLen -- how many; dst -- destination; dstLen
 |   -- exactly how many bytes the page is; stride -- bytes per row; rowStep --
 |   the value the encoder used, whose dropped scanlines are filled by repeating
 |   the one above
 | Returns: true when the whole page was reconstructed
 ----------------------*/
bool pageDeltaDecode(const uint8_t *src, int32_t srcLen, uint8_t *dst,
                     int32_t dstLen, int32_t stride, int32_t rowStep);

#endif /* PAGE_RLE_H */
