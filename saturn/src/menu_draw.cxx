/*----------------------
 | menu_draw.cxx
 | Description: Fills, ramp-based text and the frozen-frame remap over a raw
 |   4bpp page buffer. No engine headers, no SGL: see menu_draw.h for why.
 | Author: suinevere
 | Dependencies: menu_draw.h
 ----------------------*/
#include "menu_draw.h"

/*----------------------
 | menuDrawFill
 | Description: Fills an axis-aligned rectangle with a solid color, clipped to
 |   the page.
 | Author: suinevere
 | Params: page -- MENU_PAGE_SIZE bytes; x, y -- top-left corner in pixels;
 |         w, h -- size in pixels; color -- 4-bit palette index, low nibble
 | Returns: N/A
 ----------------------*/
void menuDrawFill(uint8_t *page, int x, int y, int w, int h, uint8_t color)
{
	int x0 = x;
	int y0 = y;
	int x1 = x + w;
	int y1 = y + h;

	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > MENU_PAGE_W) x1 = MENU_PAGE_W;
	if (y1 > MENU_PAGE_H) y1 = MENU_PAGE_H;

	for (int py = y0; py < y1; ++py) {
		for (int px = x0; px < x1; ++px) {
			uint8_t *b = page + py * MENU_PAGE_PITCH + px / 2;
			if (px & 1) {
				*b = (*b & 0xF0) | color;
			} else {
				*b = (*b & 0x0F) | (color << 4);
			}
		}
	}
}

/*----------------------
 | menuDrawChar
 | Description: Draws one glyph, packing two pixels per byte the same way
 |   Video::drawChar does.
 | Author: suinevere
 | Params: page -- MENU_PAGE_SIZE bytes; font -- 8 bytes per glyph starting at
 |         ' '; cellX -- column, 0..39; y -- row in scanlines, 0..192;
 |         base -- ramp base, the glyph renders at base + 2; c -- the glyph
 | Returns: N/A
 ----------------------*/
void menuDrawChar(uint8_t *page, const uint8_t *font, int cellX, int y, uint8_t base, char c)
{
	if (cellX < 0 || cellX > 39 || y < 0 || y > 192) {
		return;
	}

	const uint8_t color = (uint8_t)(base + 2);
	const uint8_t *ft = font + (c - ' ') * 8;
	uint8_t *p = page + cellX * 4 + y * MENU_PAGE_PITCH;

	for (int j = 0; j < 8; ++j) {
		uint8_t ch = ft[j];
		for (int i = 0; i < 4; ++i) {
			uint8_t b = p[i];
			uint8_t cmask = 0xFF;
			uint8_t colb = 0;
			if (ch & 0x80) {
				colb |= color << 4;
				cmask &= 0x0F;
			}
			ch <<= 1;
			if (ch & 0x80) {
				colb |= color;
				cmask &= 0xF0;
			}
			ch <<= 1;
			p[i] = (b & cmask) | colb;
		}
		p += MENU_PAGE_PITCH;
	}
}

/*----------------------
 | menuDrawText
 | Description: Draws a string one glyph per cell, left to right, stopping at
 |   cell 40 rather than wrapping.
 | Author: suinevere
 | Params: page -- MENU_PAGE_SIZE bytes; font -- glyph table, see menuDrawChar;
 |         cellX -- starting column; y -- row in scanlines; base -- ramp base;
 |         s -- NUL-terminated string
 | Returns: N/A
 ----------------------*/
void menuDrawText(uint8_t *page, const uint8_t *font, int cellX, int y, uint8_t base, const char *s)
{
	int cx = cellX;
	while (*s != '\0' && cx < 40) {
		menuDrawChar(page, font, cx, y, base, *s);
		++cx;
		++s;
	}
}

/*----------------------
 | menuFreezeRemap
 | Description: Collapses a frozen game frame onto palette indices 0..3 by
 |   luminance, in place, freeing indices 4..15 for menu artwork.
 | Author: suinevere
 | Params: page -- MENU_PAGE_SIZE bytes, remapped in place; srcPalette --
 |         32 bytes, the game palette the page was drawn against
 | Returns: N/A
 ----------------------*/
void menuFreezeRemap(uint8_t *page, const uint8_t *srcPalette)
{
	uint8_t map[16];
	for (int i = 0; i < 16; ++i) {
		const uint8_t b0 = srcPalette[i * 2];
		const uint8_t b1 = srcPalette[i * 2 + 1];
		const int r = b0 & 0x0F;
		const int g = (b1 & 0xF0) >> 4;
		const int b = b1 & 0x0F;
		const int y = (r * 77 + g * 151 + b * 28) >> 8;
		map[i] = (uint8_t)(y >> 2);
	}

	uint8_t lut8[256];
	for (int i = 0; i < 256; ++i) {
		lut8[i] = (uint8_t)((map[i >> 4] << 4) | map[i & 0x0F]);
	}

	for (int i = 0; i < MENU_PAGE_SIZE; ++i) {
		page[i] = lut8[page[i]];
	}
}
