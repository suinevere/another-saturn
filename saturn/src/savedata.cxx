/*----------------------
 | savedata.cxx
 | Description: Save slot metadata: slot naming, header packing/unpacking,
 |   slot probing, and chapter names.
 | Author: suinevere
 | Dependencies: savedata.h, serializer.h
 ----------------------*/
#include "savedata.h"
#include "serializer.h"

extern "C" {
#include <string.h>
}

/*----------------------
 | savedataSlotName
 | Description: Builds the BUP filename for a slot index.
 | Author: suinevere
 | Params: slot -- 0-based slot index; out -- destination, must hold 12 bytes
 | Returns: N/A
 ----------------------*/
void savedataSlotName(int slot, char *out)
{
	strcpy(out, "AW_SAVE");
	out[7] = (char)('1' + slot);
	out[8] = 0;
}

/*----------------------
 | savedataWriteHeader
 | Description: Packs a SAVE_HEADER_SIZE-byte save header in place, big-endian,
 |   with the current save format version and a zero-filled description.
 |   Field layout: 0 magic 'AWSV', 4 version, 6 reserved, 8 description[32],
 |   40 part id, 42 date, 46 reserved.
 | Author: suinevere
 | Params: buf -- destination, must hold SAVE_HEADER_SIZE bytes; partId -- the
 |   game part the save was made in; date -- packed BUP date
 | Returns: N/A
 ----------------------*/
void savedataWriteHeader(uint8_t *buf, uint16_t partId, uint32_t date)
{
	buf[0] = 'A';
	buf[1] = 'W';
	buf[2] = 'S';
	buf[3] = 'V';
	buf[4] = (uint8_t)((Serializer::CUR_VER >> 8) & 0xFF);
	buf[5] = (uint8_t)(Serializer::CUR_VER & 0xFF);
	buf[6] = 0;
	buf[7] = 0;
	memset(buf + 8, 0, 32);
	buf[40] = (uint8_t)((partId >> 8) & 0xFF);
	buf[41] = (uint8_t)(partId & 0xFF);
	buf[42] = (uint8_t)((date >> 24) & 0xFF);
	buf[43] = (uint8_t)((date >> 16) & 0xFF);
	buf[44] = (uint8_t)((date >> 8) & 0xFF);
	buf[45] = (uint8_t)(date & 0xFF);
	buf[46] = 0;
	buf[47] = 0;
}

/*----------------------
 | savedataReadHeader
 | Description: Unpacks a save header, checking the magic first.
 | Author: suinevere
 | Params: buf -- source, at least SAVE_HEADER_SIZE bytes; ver, partId, date --
 |   outputs, left untouched on failure
 | Returns: false on magic mismatch, true otherwise
 ----------------------*/
bool savedataReadHeader(const uint8_t *buf, uint16_t *ver, uint16_t *partId,
                        uint32_t *date)
{
	if (buf[0] != 'A' || buf[1] != 'W' || buf[2] != 'S' || buf[3] != 'V') {
		return false;
	}
	*ver = (uint16_t)((buf[4] << 8) | buf[5]);
	*partId = (uint16_t)((buf[40] << 8) | buf[41]);
	*date = ((uint32_t)buf[42] << 24) | ((uint32_t)buf[43] << 16) |
	        ((uint32_t)buf[44] << 8) | (uint32_t)buf[45];
	return true;
}

/*----------------------
 | savedataProbe
 | Description: Reads a whole slot into a static SAVE_MAX_BYTES buffer and
 |   parses the header out of it. Never reads only SAVE_HEADER_SIZE bytes:
 |   sat_bup_read refuses when the stored file is larger than the destination,
 |   and a real save is far bigger than the header alone.
 | Author: suinevere
 | Globals: s_probeBuf
 | Params: device -- SAT_BUP_INTERNAL or SAT_BUP_CART; slot -- 0-based slot
 |   index; out -- filled in
 | Returns: the same state written to out->state
 ----------------------*/
static uint8_t s_probeBuf[SAVE_MAX_BYTES];

SlotState savedataProbe(uint32_t device, int slot, SlotInfo *out)
{
	char name[12];
	savedataSlotName(slot, name);

	out->state = SLOT_EMPTY;
	out->partId = 0;
	out->date = 0;

	SatBupEntry entry;
	int dirRc = sat_bup_dir(device, name, &entry);
	if (dirRc != SAT_BUP_OK || !entry.exists) {
		return SLOT_EMPTY;
	}

	int readRc = sat_bup_read(device, name, s_probeBuf, SAVE_MAX_BYTES);
	if (readRc != SAT_BUP_OK) {
		out->state = SLOT_DAMAGED;
		return SLOT_DAMAGED;
	}

	uint16_t ver = 0, partId = 0;
	uint32_t date = 0;
	if (!savedataReadHeader(s_probeBuf, &ver, &partId, &date)) {
		out->state = SLOT_DAMAGED;
		return SLOT_DAMAGED;
	}

	if (ver < Serializer::CUR_VER) {
		out->state = SLOT_OLD_VERSION;
		out->partId = partId;
		out->date = date;
		return SLOT_OLD_VERSION;
	}

	out->state = SLOT_OK;
	out->partId = partId;
	out->date = date;
	return SLOT_OK;
}

/*----------------------
 | savedataChapterName
 | Description: Maps a game part id to the name the slot row shows. Covers
 |   GAME_PART2 (0x3E81) through GAME_PART10 (0x3E89); GAME_PART1, the
 |   copy-protection wheel, is unreachable under BYPASS_PROTECTION and falls
 |   into UNKNOWN along with any other value.
 | Author: suinevere
 | Params: partId -- a GAME_PART* value
 | Returns: a name of at most 14 characters, or "UNKNOWN"
 ----------------------*/
const char *savedataChapterName(uint16_t partId)
{
	switch (partId) {
	case 0x3E81: return "INTRO";
	case 0x3E82: return "THE ARRIVAL";
	case 0x3E83: return "THE JAIL";
	case 0x3E84: return "THE ESCAPE";
	case 0x3E85: return "THE CAVERNS";
	case 0x3E86: return "THE BATHS";
	case 0x3E87: return "THE CITY";
	case 0x3E88: return "THE ARENA";
	case 0x3E89: return "THE FINAL";
	default:     return "UNKNOWN";
	}
}
