/*----------------------
 | saturn_backup.cxx
 | Description: The Saturn side of saturn_backup.h, over SGL's BUP vector table
 |   and SRL's RTC. The only file in the port that includes sega_bup.h.
 | Author: suinevere
 | Dependencies: srl.hpp, sega_bup.h, saturn_backup.h, bup_devmap.h
 | Globals: s_bupWork, s_bupCfg
 ----------------------*/
#include <srl.hpp>
#include "sega_bup.h"
#include "saturn_backup.h"
#include "bup_devmap.h"

/* SGL's <string.h> has no extern "C" guard of its own; see saturn_cdfile.cxx. */
extern "C" {
#include <string.h>
}

/*----------------------
 | SAVE_MAX_BYTES
 | Description: The BUP_Stat datasize probe. Mirrors savedata.h's constant of
 |   the same name and value, but this layer must not depend on that one, so
 |   it is defined again here.
 | Author: suinevere
 ----------------------*/
#define SAVE_MAX_BYTES 2048

/*----------------------
 | s_bupWork
 | Description: Work area the BIOS backup library needs. Sized for the largest
 |   directory the port can create. A plain static, which SH-2 GCC places in
 |   High Work RAM's BSS -- the engine's 600 KB resource block lives in Low
 |   Work RAM, and the two must not overlap. Verified against the link map;
 |   see the task 2 report.
 | Author: suinevere
 ----------------------*/
static uint32_t s_bupWork[0x1000];

/*----------------------
 | s_bupCfg
 | Description: Handed to BUP_Init, which retains the pointer, so it is kept
 |   alive for the lifetime of the program. Its contents are NOT a per-device
 |   descriptor: on hardware every entry reads back unit_id 2, partition 2,
 |   including the entry for a slot holding nothing. Nothing here distinguishes
 |   one device from another -- sat_bup_init probes instead.
 | Author: suinevere
 ----------------------*/
static BupConfig s_bupCfg[3];

/*----------------------
 | s_internalIdx / s_cartIdx
 | Description: Which BIOS device index each logical device is, resolved once by
 |   sat_bup_init from which indices answer a probe. SAT_BUP_INTERNAL and
 |   SAT_BUP_CART are this port's logical names and carry SGL's unit id values,
 |   which are NOT device indices -- before this existed the ids went straight
 |   through, so every save the port wrote landed on the cartridge and the
 |   cartridge itself probed as absent. See bup_devmap.h for the two wrong
 |   readings of the BUP API that preceded this one.
 |
 |   Defaults match the measured hardware layout so a call arriving before
 |   sat_bup_init reaches the main unit rather than nothing.
 | Author: suinevere
 ----------------------*/
static int s_internalIdx = 0;
static int s_cartIdx = 1;

/*----------------------
 | sat_bup_hw
 | Description: Translates a logical SAT_BUP_* device into its BIOS device index.
 | Author: suinevere
 | Dependencies: bup_devmap.h
 | Globals: s_internalIdx, s_cartIdx
 | Params: device -- SAT_BUP_INTERNAL or SAT_BUP_CART
 | Returns: the index, or BUP_DEVMAP_NONE for a device this machine lacks or a
 |   logical id this file does not know
 ----------------------*/
static int sat_bup_hw(uint32_t device)
{
    if (device == SAT_BUP_INTERNAL) {
        return s_internalIdx;
    }
    if (device == SAT_BUP_CART) {
        return s_cartIdx;
    }
    return BUP_DEVMAP_NONE;
}

/*----------------------
 | sat_bup_map_error
 | Description: Translates a raw BUP library return code into this file's
 |   device-agnostic error codes. sega_bup.h defines no BUP_OK constant --
 |   success is the literal 0 -- and no negative codes, so 0 is the only value
 |   that may map to SAT_BUP_OK; every other value, known or not, is an error.
 | Author: suinevere
 | Params: rc -- BUP_* return code, or 0 for success
 | Returns: a SAT_BUP_* code
 ----------------------*/
static int sat_bup_map_error(int32_t rc)
{
    switch (rc) {
    case 0:                        return SAT_BUP_OK;
    case BUP_NON:                  return SAT_BUP_ERR_NONE;
    case BUP_UNFORMAT:             return SAT_BUP_ERR_UNFORMAT;
    case BUP_WRITE_PROTECT:        return SAT_BUP_ERR_PROTECTED;
    case BUP_NOT_ENOUGH_MEMORY:    return SAT_BUP_ERR_NO_SPACE;
    case BUP_NOT_FOUND:            return SAT_BUP_ERR_NOT_FOUND;
    case BUP_FOUND:                return SAT_BUP_ERR_EXISTS;
    case BUP_NO_MATCH:             return SAT_BUP_ERR_BROKEN;
    case BUP_BROKEN:               return SAT_BUP_ERR_BROKEN;
    default:                       return SAT_BUP_ERR_BROKEN;
    }
}

/*----------------------
 | sat_bup_init
 | Description: Brings up the BIOS backup library, then works out which device
 |   index is which by asking each one whether it is there.
 | Author: suinevere
 | Dependencies: bup_devmap.h
 | Globals: s_bupWork, s_bupCfg, s_internalIdx, s_cartIdx
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void sat_bup_init(void)
{
    int present[3];

    BUP_Init((uint32_t *)BUP_LIB_ADDRESS, s_bupWork, s_bupCfg);

    for (int i = 0; i < 3; ++i) {
        BupStat st;
        const int32_t rc = BUP_Stat((uint32_t)i, SAVE_MAX_BYTES, &st);
        present[i] = (rc != BUP_NON) ? 1 : 0;
    }
    bupDevmapResolve(present, 3, &s_internalIdx, &s_cartIdx);
}

/*----------------------
 | sat_bup_probe
 | Description: Reports whether a device is present, formatted, writable, and
 |   how much room it has left.
 | Author: suinevere
 | Params: device -- SAT_BUP_INTERNAL or SAT_BUP_CART; out -- filled in
 | Returns: SAT_BUP_OK, or an error code with out zeroed
 ----------------------*/
extern "C" int sat_bup_probe(uint32_t device, SatBupDev *out)
{
    BupStat st;
    memset(out, 0, sizeof(*out));

    const int hw = sat_bup_hw(device);
    if (hw == BUP_DEVMAP_NONE) {
        return SAT_BUP_ERR_NONE;
    }

    int32_t rc = BUP_Stat((uint32_t)hw, SAVE_MAX_BYTES, &st);
    if (rc == BUP_NON) {
        return SAT_BUP_ERR_NONE;
    }
    out->present = 1;
    if (rc == BUP_UNFORMAT) {
        return SAT_BUP_ERR_UNFORMAT;
    }
    out->formatted = 1;
    if (rc == BUP_WRITE_PROTECT) {
        out->writeProtected = 1;
        return SAT_BUP_ERR_PROTECTED;
    }
    out->freeBytes = st.freesize;
    return SAT_BUP_OK;
}

/*----------------------
 | sat_bup_dir
 | Description: Looks a save up by name without reading its contents.
 |   BUP_Dir is the one call in this file that returns a match count rather
 |   than a status: negative is a real error, 0 is a clean not-found, and any
 |   positive count means out was filled in.
 | Author: suinevere
 | Params: device -- device id; name -- BUP filename; out -- filled in
 | Returns: SAT_BUP_OK whether or not the file exists; check out->exists. An
 |   error code means the lookup itself failed, and out is left zeroed.
 ----------------------*/
extern "C" int sat_bup_dir(uint32_t device, const char *name, SatBupEntry *out)
{
    BupDir dir;
    memset(out, 0, sizeof(*out));
    memset(&dir, 0, sizeof(dir));

    const int hw = sat_bup_hw(device);
    if (hw == BUP_DEVMAP_NONE) {
        return SAT_BUP_ERR_NONE;
    }

    int32_t rc = BUP_Dir((uint32_t)hw, (uint8_t *)name, 1, &dir);
    if (rc < 0) {
        return sat_bup_map_error(rc);
    }
    if (rc == 0) {
        return SAT_BUP_OK;
    }
    out->exists = 1;
    out->size = dir.datasize;
    out->date = dir.date;
    return SAT_BUP_OK;
}

/*----------------------
 | sat_bup_read
 | Description: Reads a whole save into dst. BUP_Read itself has no
 |   destination-capacity argument, so the size check happens up front via a
 |   BUP_Dir lookup: if the stored file is bigger than dst, the read is
 |   refused before BUP_Read ever runs.
 | Author: suinevere
 | Params: device -- device id; name -- BUP filename; dst -- destination;
 |   size -- capacity of dst
 | Returns: SAT_BUP_OK, or a mapped error, or SAT_BUP_ERR_BROKEN if the stored
 |   file is larger than size
 ----------------------*/
extern "C" int sat_bup_read(uint32_t device, const char *name, void *dst,
                            int32_t size)
{
    BupDir dir;
    memset(&dir, 0, sizeof(dir));

    const int hw = sat_bup_hw(device);
    if (hw == BUP_DEVMAP_NONE) {
        return SAT_BUP_ERR_NONE;
    }

    int32_t dirRc = BUP_Dir((uint32_t)hw, (uint8_t *)name, 1, &dir);
    if (dirRc < 0) {
        return sat_bup_map_error(dirRc);
    }
    if (dirRc == 0) {
        return SAT_BUP_ERR_NOT_FOUND;
    }
    if (dir.datasize > (uint32_t)size) {
        return SAT_BUP_ERR_BROKEN;
    }

    int32_t rc = BUP_Read((uint32_t)hw, (uint8_t *)name, (uint8_t *)dst);
    return sat_bup_map_error(rc);
}

/*----------------------
 | sat_bup_fill_dir
 | Description: Fills a BupDir for a write, stamping it with the current RTC
 |   time. Separate because an overwrite has to build it twice.
 | Author: suinevere
 | Params: dir -- the directory entry to fill; name -- BUP filename; comment --
 |   up to 10 characters; size -- how many bytes the save is
 | Returns: N/A
 ----------------------*/
static void sat_bup_fill_dir(BupDir *dir, const char *name,
                             const char *comment, int32_t size)
{
    memset(dir, 0, sizeof(*dir));
    strncpy((char *)dir->filename, name, 11);
    strncpy((char *)dir->comment, comment, 10);
    dir->language = BUP_ENGLISH;
    dir->date = sat_bup_date_now();
    dir->datasize = (uint32_t)size;
    dir->blocksize = 0;
}

/*----------------------
 | sat_bup_write
 | Description: Writes a save, stamping it with the current RTC time. The BIOS
 |   refuses an existing filename with BUP_FOUND even when wmode asks for an
 |   overwrite -- measured on hardware, and sega_bup.h documents no wmode
 |   constants to argue with -- so a refused overwrite deletes the entry and
 |   writes again rather than trusting the flag.
 | Author: suinevere
 | Params: device -- device id; name -- BUP filename; comment -- up to 10
 |   characters; src -- the bytes; size -- how many; overwrite -- non-zero to
 |   replace an existing file
 | Returns: SAT_BUP_OK, or a mapped error
 ----------------------*/
extern "C" int sat_bup_write(uint32_t device, const char *name,
                             const char *comment, const void *src,
                             int32_t size, int overwrite)
{
    BupDir dir;
    const int hw = sat_bup_hw(device);
    if (hw == BUP_DEVMAP_NONE) {
        return SAT_BUP_ERR_NONE;
    }

    sat_bup_fill_dir(&dir, name, comment, size);

    int32_t rc = BUP_Write((uint32_t)hw, &dir, (uint8_t *)src,
                           overwrite ? 1 : 0);

    if (overwrite && rc == BUP_FOUND) {
        BUP_Delete((uint32_t)hw, (uint8_t *)name);
        sat_bup_fill_dir(&dir, name, comment, size);
        rc = BUP_Write((uint32_t)hw, &dir, (uint8_t *)src, 1);
    }

    return sat_bup_map_error(rc);
}

/*----------------------
 | sat_bup_delete
 | Description: Removes a save.
 | Author: suinevere
 | Params: device -- device id; name -- BUP filename
 | Returns: SAT_BUP_OK, or a mapped error
 ----------------------*/
extern "C" int sat_bup_delete(uint32_t device, const char *name)
{
    const int hw = sat_bup_hw(device);
    if (hw == BUP_DEVMAP_NONE) {
        return SAT_BUP_ERR_NONE;
    }
    return sat_bup_map_error(BUP_Delete((uint32_t)hw, (uint8_t *)name));
}

/*----------------------
 | sat_bup_date_now
 | Description: The current RTC time as a BUP date word.
 | Author: suinevere
 | Returns: the packed word
 ----------------------*/
extern "C" uint32_t sat_bup_date_now(void)
{
    SRL::Types::DateTime now = SRL::Types::DateTime::Now();
    BupDate d = now.ToBackupUnitDate();
    return BUP_SetDate(&d);
}

/*----------------------
 | sat_bup_date_split
 | Description: Unpacks a BUP date word into the fields the slot list shows.
 |   Copied verbatim from stub_saturn_backup.cxx, the copy the host tests
 |   prove correct.
 | Author: suinevere
 | Params: date -- packed word; month, day, hour, min -- outputs, any may be
 |   NULL
 | Returns: N/A
 ----------------------*/
extern "C" void sat_bup_date_split(uint32_t date, int *month, int *day,
                                   int *hour, int *min)
{
    static const int len[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

    uint32_t days = date / 1440u;
    uint32_t rem = date % 1440u;

    int year = 1980;
    for (;;) {
        int inYear = ((year % 4) == 0) ? 366 : 365;
        if (days < (uint32_t)inYear) {
            break;
        }
        days -= (uint32_t)inYear;
        year++;
    }

    int mo = 0;
    for (;;) {
        int inMonth = len[mo];
        if (mo == 1 && (year % 4) == 0) {
            inMonth = 29;
        }
        if (days < (uint32_t)inMonth) {
            break;
        }
        days -= (uint32_t)inMonth;
        mo++;
    }

    if (month) *month = mo + 1;
    if (day)   *day = (int)days + 1;
    if (hour)  *hour = (int)(rem / 60u);
    if (min)   *min = (int)(rem % 60u);
}
