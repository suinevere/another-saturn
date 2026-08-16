/*----------------------
 | settings.h
 | Description: The player's preferences -- the record kept in backup RAM and
 |   the pure mapping from face buttons to the two actions the VM can hear.
 |   Deliberately free of engine and menu headers, which is what keeps it
 |   host-testable; the same split savedata.h uses.
 | Author: suinevere
 | Dependencies: saturn_backup.h
 ----------------------*/
#ifndef SETTINGS_H
#define SETTINGS_H

#include "saturn_backup.h"

/*----------------------
 | SETTINGS_SIZE / SETTINGS_VER
 | Description: The stored record's byte count, and its own format version --
 |   not the save format's, since preferences change on a different schedule
 |   from saved games.
 | Author: suinevere
 ----------------------*/
enum {
	SETTINGS_SIZE = 16,
	SETTINGS_VER  = 1
};

/*----------------------
 | Settings
 | Description: Everything the player can configure. swapButtons false means A
 |   and C act while B jumps; true means the reverse.
 | Author: suinevere
 ----------------------*/
struct Settings {
	bool swapButtons;
};

/*----------------------
 | settingsDefaults
 | Description: Fills a Settings with the shipping defaults.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: s -- filled in
 | Returns: N/A
 ----------------------*/
void settingsDefaults(Settings *s);

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
void settingsPack(uint8_t *buf, const Settings *s);

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
bool settingsUnpack(const uint8_t *buf, Settings *s);

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
bool settingsLoad(Settings *s);

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
int settingsStore(const Settings *s);

/*----------------------
 | settingsMapFaceButtons
 | Description: Resolves the three face buttons into the two signals the VM can
 |   hear. A and C are always the same action as each other and B is always the
 |   other one, so the layout is one bit. Takes plain booleans rather than
 |   SAT_PAD_* bits to keep this header free of platform includes.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: a, b, c -- whether each face button is held; swap -- the layout;
 |   jump, action -- outputs, always written
 | Returns: N/A
 ----------------------*/
void settingsMapFaceButtons(bool a, bool b, bool c, bool swap,
                            bool *jump, bool *action);

#endif /* SETTINGS_H */
