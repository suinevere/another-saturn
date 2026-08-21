/*----------------------
 | settings.cxx
 | Description: Preference record packing and the face button mapping.
 | Author: suinevere
 | Dependencies: settings.h, saturn_backup.h
 ----------------------*/
#include "settings.h"

extern "C" {
#include <string.h>
}

/*----------------------
 | settingsDefaults
 | Description: Fills a Settings with the shipping defaults.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: s -- filled in
 | Returns: N/A
 ----------------------*/
void settingsDefaults(Settings *s)
{
	s->swapButtons = false;
}

/*----------------------
 | settingsPack
 | Description: Packs a SETTINGS_SIZE-byte record in place, big-endian, zeroing
 |   the reserved tail. Field layout: 0 magic 'AWCF', 4 version, 6 flags,
 |   7 reserved.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: buf -- destination, must hold SETTINGS_SIZE bytes; s -- source
 | Returns: N/A
 ----------------------*/
void settingsPack(uint8_t *buf, const Settings *s)
{
	memset(buf, 0, SETTINGS_SIZE);
	buf[0] = 'A';
	buf[1] = 'W';
	buf[2] = 'C';
	buf[3] = 'F';
	buf[4] = (uint8_t)((SETTINGS_VER >> 8) & 0xFF);
	buf[5] = (uint8_t)(SETTINGS_VER & 0xFF);
	buf[6] = (uint8_t)(s->swapButtons ? 1 : 0);
}

/*----------------------
 | settingsUnpack
 | Description: Unpacks a record, checking the magic and then the version.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: buf -- source, at least SETTINGS_SIZE bytes; s -- output, left
 |   untouched on failure
 | Returns: false on magic mismatch or unknown version, true otherwise
 ----------------------*/
bool settingsUnpack(const uint8_t *buf, Settings *s)
{
	if (buf[0] != 'A' || buf[1] != 'W' || buf[2] != 'C' || buf[3] != 'F') {
		return false;
	}
	const uint16_t ver = (uint16_t)((buf[4] << 8) | buf[5]);
	if (ver != SETTINGS_VER) {
		return false;
	}
	s->swapButtons = (buf[6] & 1) != 0;
	return true;
}

/*----------------------
 | SETTINGS_NAME
 | Description: The backup RAM entry the record lives in, alongside the
 |   AW_SAVE1..3 slots savedata.cxx owns.
 | Author: suinevere
 ----------------------*/
static const char SETTINGS_NAME[] = "AW_CFG";

/*----------------------
 | settingsLoad
 | Description: Reads the record from internal backup RAM. Internal only, not
 |   the device the slot list picked: internal is always fitted, so the config
 |   cannot vanish with a cartridge, and the load runs at boot before any
 |   device has been probed.
 | Author: suinevere
 | Dependencies: saturn_backup.h
 | Globals: N/A
 | Params: s -- output, left untouched unless a valid record is read, so a
 |   caller that seeded it with settingsDefaults keeps those defaults
 | Returns: true only when a valid record was read
 ----------------------*/
bool settingsLoad(Settings *s)
{
	uint8_t buf[SETTINGS_SIZE];

	if (sat_bup_read(SAT_BUP_INTERNAL, SETTINGS_NAME, buf, SETTINGS_SIZE)
	    != SAT_BUP_OK) {
		return false;
	}
	return settingsUnpack(buf, s);
}

/*----------------------
 | settingsStore
 | Description: Writes the record to internal backup RAM, replacing any record
 |   already there.
 | Author: suinevere
 | Dependencies: saturn_backup.h
 | Globals: N/A
 | Params: s -- the settings to write
 | Returns: SAT_BUP_OK, or the SAT_BUP_ERR_* code the write failed with
 ----------------------*/
int settingsStore(const Settings *s)
{
	uint8_t buf[SETTINGS_SIZE];

	settingsPack(buf, s);
	return sat_bup_write(SAT_BUP_INTERNAL, SETTINGS_NAME, "BUTTONS", buf,
	                     SETTINGS_SIZE, 1);
}

/*----------------------
 | settingsMapFaceButtons
 | Description: Gives each face button one action -- attack, run and jump --
 |   and the layout bit swaps which of A and C attacks. B is always run.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: a, b, c -- whether each face button is held; swap -- the layout;
 |   jump, action -- outputs, always written
 | Returns: N/A
 ----------------------*/
void settingsMapFaceButtons(bool a, bool b, bool c, bool swap,
                            bool *jump, bool *action, bool *run)
{
	*action = swap ? c : a;
	*jump   = swap ? a : c;
	*run    = b;
}
