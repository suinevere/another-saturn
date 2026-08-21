/*----------------------
 | test_savedata.cxx
 | Description: Host unit tests for savedata.cxx: header packing, slot probing
 |   against the backup stub, and backup device defaulting.
 | Author: suinevere
 | Dependencies: savedata.h, saturn_backup.h
 ----------------------*/
#include <cstdint>
#include "savedata.h"
#include "saturn_backup.h"

/* Neither <cstdio> nor <cstring> is included here: savedata.h pulls in
   intern.h, which already declares printf (via saturn_compat.h) and
   strcmp/memset (via its own extern "C" <string.h>) -- including the C++
   wrappers on top collides with saturn_compat.h's FILE typedef. See
   savedata.h and intern.h for the full story. */

extern void stub_bup_reset(void);
extern void stub_bup_set_device(uint32_t device, int present, int formatted,
                                int writeProtected, uint32_t freeBytes);
extern void stub_bup_add_file(uint32_t device, const char *name,
                              const void *data, int32_t size, uint32_t date);

static int g_fail = 0;

#define CHECK_EQ(actual, expected)                                            \
    do {                                                                      \
        long long a_ = (long long)(actual);                                   \
        long long e_ = (long long)(expected);                                 \
        if (a_ != e_) {                                                       \
            g_fail++;                                                         \
            printf("FAIL %s:%d  %s\n  actual   = %lld\n  expected = %lld\n",  \
                   __FILE__, __LINE__, #actual, a_, e_);                      \
        }                                                                     \
    } while (0)

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            g_fail++;                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                     \
    } while (0)

static void test_slot_names(void)
{
    char n[12];
    savedataSlotName(0, n);
    CHECK(strcmp(n, "AW_SAVE1") == 0);
    savedataSlotName(2, n);
    CHECK(strcmp(n, "AW_SAVE3") == 0);
}

static void test_header_round_trip(void)
{
    uint8_t buf[SAVE_HEADER_SIZE];
    memset(buf, 0, sizeof(buf));
    savedataWriteHeader(buf, 0x3E83, 0x00112233);

    uint16_t ver = 0, part = 0;
    uint32_t date = 0;
    CHECK(savedataReadHeader(buf, &ver, &part, &date));
    CHECK_EQ(ver, 3);
    CHECK_EQ(part, 0x3E83);
    CHECK_EQ(date, 0x00112233);
}

static void test_header_rejects_bad_magic(void)
{
    uint8_t buf[SAVE_HEADER_SIZE];
    memset(buf, 0, sizeof(buf));
    savedataWriteHeader(buf, 0x3E83, 0);
    buf[1] = 'X';

    uint16_t ver = 0, part = 0;
    uint32_t date = 0;
    CHECK(!savedataReadHeader(buf, &ver, &part, &date));
}

static void test_probe_empty_slot(void)
{
    stub_bup_reset();
    SlotInfo info;
    CHECK_EQ(savedataProbe(SAT_BUP_INTERNAL, 0, &info), SLOT_EMPTY);
    CHECK_EQ(info.state, SLOT_EMPTY);
}

static void test_probe_good_slot(void)
{
    stub_bup_reset();
    uint8_t blob[SAVE_HEADER_SIZE];
    memset(blob, 0, sizeof(blob));
    savedataWriteHeader(blob, 0x3E83, 4242);
    stub_bup_add_file(SAT_BUP_INTERNAL, "AW_SAVE1", blob, sizeof(blob), 4242);

    SlotInfo info;
    CHECK_EQ(savedataProbe(SAT_BUP_INTERNAL, 0, &info), SLOT_OK);
    CHECK_EQ(info.partId, 0x3E83);
    CHECK_EQ(info.date, 4242);
}

static void test_probe_damaged_slot(void)
{
    stub_bup_reset();
    uint8_t blob[SAVE_HEADER_SIZE];
    memset(blob, 0xFF, sizeof(blob));
    stub_bup_add_file(SAT_BUP_INTERNAL, "AW_SAVE1", blob, sizeof(blob), 0);

    SlotInfo info;
    CHECK_EQ(savedataProbe(SAT_BUP_INTERNAL, 0, &info), SLOT_DAMAGED);
}

static void test_probe_old_version(void)
{
    stub_bup_reset();
    uint8_t blob[SAVE_HEADER_SIZE];
    memset(blob, 0, sizeof(blob));
    savedataWriteHeader(blob, 0x3E83, 0);
    blob[5] = 2;
    stub_bup_add_file(SAT_BUP_INTERNAL, "AW_SAVE1", blob, sizeof(blob), 0);

    SlotInfo info;
    CHECK_EQ(savedataProbe(SAT_BUP_INTERNAL, 0, &info), SLOT_OLD_VERSION);
}

static void test_chapter_names(void)
{
    CHECK(savedataChapterName(0x3E81) != 0);
    CHECK(savedataChapterName(0x3E89) != 0);
    CHECK(strcmp(savedataChapterName(0x1234), "UNKNOWN") == 0);
}

int main(void)
{
    test_slot_names();
    test_header_round_trip();
    test_header_rejects_bad_magic();
    test_probe_empty_slot();
    test_probe_good_slot();
    test_probe_damaged_slot();
    test_probe_old_version();
    test_chapter_names();

    if (g_fail == 0) {
        printf("all tests passed\n");
        return 0;
    }
    printf("%d check(s) failed\n", g_fail);
    return 1;
}
