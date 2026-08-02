/*----------------------
 | saturn_backup.cxx
 | Description: The Saturn side of saturn_backup.h, over SGL's BUP vector table
 |   and SRL's RTC. The only file in the port that includes sega_bup.h.
 | Author: suinevere
 | Dependencies: srl.hpp, sega_bup.h, saturn_backup.h
 | Globals: s_bupWork, s_bupCfg
 ----------------------*/
#include <srl.hpp>
#include "sega_bup.h"
#include "saturn_backup.h"

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
static uint32_t s_bupWork[0x2000 / 4];

/*----------------------
 | s_bupCfg
 | Description: BUP_Init fills one config per device. Kept alive for the
 |   lifetime of the program because the library retains the pointer.
 | Author: suinevere
 ----------------------*/
static BupConfig s_bupCfg[3];

/*----------------------
 | sat_bup_map_error
 | Description: Translates a raw BUP library return code into this file's
 |   device-agnostic error codes.
 | Author: suinevere
 | Params: rc -- BUP_* return code
 | Returns: a SAT_BUP_* code
 ----------------------*/
static int sat_bup_map_error(int32_t rc)
{
    switch (rc) {
    case BUP_NON:                  return SAT_BUP_ERR_NONE;
    case BUP_UNFORMAT:             return SAT_BUP_ERR_UNFORMAT;
    case BUP_WRITE_PROTECT:        return SAT_BUP_ERR_PROTECTED;
    case BUP_NOT_ENOUGH_MEMORY:    return SAT_BUP_ERR_NO_SPACE;
    case BUP_NOT_FOUND:            return SAT_BUP_ERR_NOT_FOUND;
    case BUP_BROKEN:               return SAT_BUP_ERR_BROKEN;
    default:                       return SAT_BUP_OK;
    }
}

/*----------------------
 | sat_bup_init
 | Description: Brings up the BIOS backup library.
 | Author: suinevere
 | Globals: s_bupWork, s_bupCfg
 | Returns: N/A
 ----------------------*/
extern "C" void sat_bup_init(void)
{
    BUP_Init((uint32_t *)BUP_LIB_ADDRESS, s_bupWork, s_bupCfg);
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

    int32_t rc = BUP_Stat(device, SAVE_MAX_BYTES, &st);
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
 | Author: suinevere
 | Params: device -- device id; name -- BUP filename; out -- filled in
 | Returns: SAT_BUP_OK whether or not the file exists; check out->exists
 ----------------------*/
extern "C" int sat_bup_dir(uint32_t device, const char *name, SatBupEntry *out)
{
    BupDir dir;
    memset(out, 0, sizeof(*out));
    memset(&dir, 0, sizeof(dir));

    int32_t rc = BUP_Dir(device, (uint8_t *)name, 1, &dir);
    if (rc <= 0) {
        return SAT_BUP_OK;
    }
    out->exists = 1;
    out->size = dir.datasize;
    out->date = dir.date;
    return SAT_BUP_OK;
}

/*----------------------
 | sat_bup_read
 | Description: Reads a whole save into dst.
 | Author: suinevere
 | Params: device -- device id; name -- BUP filename; dst -- destination;
 |   size -- unused, BUP_Read has no destination-capacity argument
 | Returns: SAT_BUP_OK, or a mapped error
 ----------------------*/
extern "C" int sat_bup_read(uint32_t device, const char *name, void *dst,
                            int32_t size)
{
    (void)size;
    int32_t rc = BUP_Read(device, (uint8_t *)name, (uint8_t *)dst);
    return sat_bup_map_error(rc);
}

/*----------------------
 | sat_bup_write
 | Description: Writes a save, stamping it with the current RTC time.
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
    memset(&dir, 0, sizeof(dir));
    strncpy((char *)dir.filename, name, 11);
    strncpy((char *)dir.comment, comment, 10);
    dir.language = BUP_ENGLISH;
    dir.date = sat_bup_date_now();
    dir.datasize = (uint32_t)size;
    dir.blocksize = 0;

    int32_t rc = BUP_Write(device, &dir, (uint8_t *)src,
                           overwrite ? 1 : 0);
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
    return sat_bup_map_error(BUP_Delete(device, (uint8_t *)name));
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
