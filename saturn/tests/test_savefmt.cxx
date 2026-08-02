/*----------------------
 | test_savefmt.cxx
 | Description: Host unit tests for the memory-backed File and for the
 |   serialiser version gating that drops video pages from a version 3 save.
 |   Built and run by run_tests.sh, never by the Saturn makefile -- that globs
 |   src/ only, so tests/ is excluded automatically.
 | Author: suinevere
 | Dependencies: file.h, serializer.h
 ----------------------*/
#include <cstdint>
#include <cstring>
#include "file.h"
#include "serializer.h"

/* g_debugMask is NOT defined here on purpose: util.cxx:23 already defines it
   and is linked into this suite. Defining it again is a duplicate symbol. */

/* stdout/stderr are declared extern in saturn_compat.h (via file.h) and are
   normally defined by saturn_compat.cxx, which pulls in srl.hpp and cannot be
   part of a host build. util.cxx's error()/warning() reference them even
   though this suite never calls those paths, so the host link still needs a
   definition; MinGW's own stdio exposes stdout/stderr only as macros, not as
   linkable symbols, so real <cstdio> cannot supply them either. */
FILE *stdout = 0;
FILE *stderr = 0;

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

static void test_memory_file_round_trip(void)
{
    uint8_t buf[64];
    memset(buf, 0, sizeof(buf));

    File w;
    CHECK(w.openMemory(buf, sizeof(buf), true));
    w.writeUint32BE(0x41575356);
    w.writeUint16BE(3);
    w.writeByte(0xAB);
    CHECK(!w.ioErr());

    CHECK_EQ(buf[0], 0x41);
    CHECK_EQ(buf[3], 0x56);
    CHECK_EQ(buf[5], 3);
    CHECK_EQ(buf[6], 0xAB);

    File r;
    CHECK(r.openMemory(buf, sizeof(buf), false));
    CHECK_EQ(r.readUint32BE(), 0x41575356);
    CHECK_EQ(r.readUint16BE(), 3);
    CHECK_EQ(r.readByte(), 0xAB);
    CHECK(!r.ioErr());
}

static void test_memory_file_overflow_sets_ioerr(void)
{
    uint8_t buf[4];
    File w;
    CHECK(w.openMemory(buf, sizeof(buf), true));
    w.writeUint32BE(0);
    CHECK(!w.ioErr());
    w.writeByte(1);
    CHECK(w.ioErr());
}

static void test_memory_file_rejects_bad_arguments(void)
{
    uint8_t buf[4];
    File a;
    CHECK(!a.openMemory(0, 4, true));
    File b;
    CHECK(!b.openMemory(buf, 0, true));
}

static void test_memory_file_seek(void)
{
    uint8_t buf[8];
    memset(buf, 0, sizeof(buf));
    File w;
    CHECK(w.openMemory(buf, sizeof(buf), true));
    w.seek(4);
    w.writeByte(0x7F);
    CHECK_EQ(buf[0], 0);
    CHECK_EQ(buf[4], 0x7F);
}

/* Mirrors Video::saveOrLoad: scalars carry the macro's CUR_VER, the page
   arrays are pinned to maxVer 2. */
static void buildVideoLikeEntries(Serializer::Entry *e, uint8_t *mask,
                                  uint8_t *pages, int pageSize)
{
    e[0].type = Serializer::SET_INT;
    e[0].size = Serializer::SES_INT8;
    e[0].n = 1;
    e[0].data = mask;
    e[0].minVer = 1;
    e[0].maxVer = Serializer::CUR_VER;

    e[1].type = Serializer::SET_ARRAY;
    e[1].size = Serializer::SES_INT8;
    e[1].n = pageSize;
    e[1].data = pages;
    e[1].minVer = 1;
    e[1].maxVer = 2;

    e[2].type = Serializer::SET_END;
    e[2].size = 0;
    e[2].n = 0;
    e[2].data = 0;
    e[2].minVer = 0;
    e[2].maxVer = 0;
}

static void test_cur_ver_is_three(void)
{
    CHECK_EQ(Serializer::CUR_VER, 3);
}

static void test_version_three_save_omits_pages(void)
{
    uint8_t buf[256];
    uint8_t mask = 0x1B;
    uint8_t pages[16];
    memset(pages, 0xEE, sizeof(pages));

    Serializer::Entry entries[3];
    buildVideoLikeEntries(entries, &mask, pages, sizeof(pages));

    File f;
    CHECK(f.openMemory(buf, sizeof(buf), true));
    Serializer s(&f, Serializer::SM_SAVE, 0);
    s.saveOrLoadEntries(entries);

    CHECK_EQ(s._bytesCount, 1);
}

static void test_version_three_load_leaves_pages_untouched(void)
{
    uint8_t buf[256];
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x1B;

    uint8_t mask = 0;
    uint8_t pages[16];
    memset(pages, 0x11, sizeof(pages));

    Serializer::Entry entries[3];
    buildVideoLikeEntries(entries, &mask, pages, sizeof(pages));

    File f;
    CHECK(f.openMemory(buf, sizeof(buf), false));
    Serializer s(&f, Serializer::SM_LOAD, 0, 3);
    s.saveOrLoadEntries(entries);

    CHECK_EQ(mask, 0x1B);
    CHECK_EQ(pages[0], 0x11);
    CHECK_EQ(s._bytesCount, 1);
}

static void test_version_two_load_still_reads_pages(void)
{
    uint8_t buf[256];
    memset(buf, 0x5A, sizeof(buf));
    buf[0] = 0x1B;

    uint8_t mask = 0;
    uint8_t pages[16];
    memset(pages, 0, sizeof(pages));

    Serializer::Entry entries[3];
    buildVideoLikeEntries(entries, &mask, pages, sizeof(pages));

    File f;
    CHECK(f.openMemory(buf, sizeof(buf), false));
    Serializer s(&f, Serializer::SM_LOAD, 0, 2);
    s.saveOrLoadEntries(entries);

    CHECK_EQ(mask, 0x1B);
    CHECK_EQ(pages[0], 0x5A);
    CHECK_EQ(s._bytesCount, 17);
}

static void test_lean_state_fits_the_slot_budget(void)
{
    static uint8_t vmVariables[0x100 * 2];
    static uint8_t stackCalls[0x100 * 2];
    static uint8_t threadsData[0x40 * 2 * 2];
    static uint8_t channelActive[0x40 * 2];
    static uint8_t loadedList[64];
    uint8_t buf[4096];

    Serializer::Entry entries[] = {
        SE_ARRAY(vmVariables, 0x100, Serializer::SES_INT16, VER(1)),
        SE_ARRAY(stackCalls, 0x100, Serializer::SES_INT16, VER(1)),
        SE_ARRAY(threadsData, 0x40 * 2, Serializer::SES_INT16, VER(1)),
        SE_ARRAY(channelActive, 0x40 * 2, Serializer::SES_INT8, VER(1)),
        SE_ARRAY(loadedList, 64, Serializer::SES_INT8, VER(1)),
        SE_END()
    };

    File f;
    CHECK(f.openMemory(buf, sizeof(buf), true));
    Serializer s(&f, Serializer::SM_SAVE, 0);
    s.saveOrLoadEntries(entries);

    CHECK_EQ(s._bytesCount, 1472);
    CHECK(s._bytesCount + 48 < 2048);
}

int main(void)
{
    test_memory_file_round_trip();
    test_memory_file_overflow_sets_ioerr();
    test_memory_file_rejects_bad_arguments();
    test_memory_file_seek();
    test_cur_ver_is_three();
    test_version_three_save_omits_pages();
    test_version_three_load_leaves_pages_untouched();
    test_version_two_load_still_reads_pages();
    test_lean_state_fits_the_slot_budget();

    if (g_fail == 0) {
        printf("all tests passed\n");
        return 0;
    }
    printf("%d check(s) failed\n", g_fail);
    return 1;
}
