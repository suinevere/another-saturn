/*----------------------
 | menu_draw.h
 | Description: Drawing primitives for the title, pause and slot-list screens:
 |   fills, text and palette dimming over a raw 4bpp page buffer. The font is
 |   passed in rather than pulled from Video, and there is no other engine
 |   dependency, so the pixel arithmetic is host-testable the same way
 |   scsp_voice.h is kept apart from saturn_scsp.cxx.
 | Author: suinevere
 | Dependencies: stdint.h
 ----------------------*/
#ifndef MENU_DRAW_H
#define MENU_DRAW_H

#include <stdint.h>

/*----------------------
 | MENU_PAGE_W / MENU_PAGE_H / MENU_PAGE_PITCH / MENU_PAGE_SIZE
 | Description: The geometry of one page buffer: 320x200 at 4bpp, 160 bytes
 |   per scanline, high nibble is the left pixel of each byte -- matching
 |   Video's page format exactly.
 | Author: suinevere
 ----------------------*/
enum {
	MENU_PAGE_W     = 320,
	MENU_PAGE_H     = 200,
	MENU_PAGE_PITCH = 160,
	MENU_PAGE_SIZE  = 32000
};

/*----------------------
 | menuDrawFill
 | Description: Fills an axis-aligned rectangle with a solid color, clipped to
 |   the page. x and y are in pixels, not cells, and may be negative or run
 |   past the page edge; only the visible portion is drawn.
 | Author: suinevere
 | Params: page -- MENU_PAGE_SIZE bytes; x, y -- top-left corner in pixels;
 |         w, h -- size in pixels; color -- 4-bit palette index, low nibble
 | Returns: N/A
 ----------------------*/
void menuDrawFill(uint8_t *page, int x, int y, int w, int h, uint8_t color);

/*----------------------
 | menuDrawChar
 | Description: Draws one glyph, packing two pixels per byte the same way
 |   Video::drawChar does. cellX is in 8-pixel cells and y is in scanlines,
 |   not pixels; out-of-range cells or rows are silently dropped.
 | Author: suinevere
 | Params: page -- MENU_PAGE_SIZE bytes; font -- 8 bytes per glyph starting at
 |         ' '; cellX -- column, 0..39; y -- row in scanlines, 0..192;
 |         color -- 4-bit palette index; c -- the glyph to draw
 | Returns: N/A
 ----------------------*/
void menuDrawChar(uint8_t *page, const uint8_t *font, int cellX, int y, uint8_t color, char c);

/*----------------------
 | menuDrawText
 | Description: Draws a string one glyph per cell, left to right, stopping at
 |   cell 40 rather than wrapping.
 | Author: suinevere
 | Params: page -- MENU_PAGE_SIZE bytes; font -- glyph table, see menuDrawChar;
 |         cellX -- starting column; y -- row in scanlines; color -- 4-bit
 |         palette index; s -- NUL-terminated string
 | Returns: N/A
 ----------------------*/
void menuDrawText(uint8_t *page, const uint8_t *font, int cellX, int y, uint8_t color, const char *s);

/*----------------------
 | menuDrawDimPalette
 | Description: Halves every channel of a 16-entry, 2-bytes-per-entry palette,
 |   except that keepIndex, when 0..15, is written as full white instead of
 |   dimmed. A keepIndex outside 0..15 dims every entry.
 | Author: suinevere
 | Params: src -- 32 bytes, source palette; dst -- 32 bytes, may not overlap
 |         src; keepIndex -- entry to keep bright, or any value outside 0..15
 |         to dim all sixteen
 | Returns: N/A
 ----------------------*/
void menuDrawDimPalette(const uint8_t *src, uint8_t *dst, int keepIndex);

#endif /* MENU_DRAW_H */
