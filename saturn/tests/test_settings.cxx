/*----------------------
 | test_settings.cxx
 | Description: Host unit tests for settings.cxx: record packing, the backup
 |   RAM round trip against the stub, and the face button mapping.
 | Author: suinevere
 | Dependencies: settings.h, saturn_backup.h
 ----------------------*/
#include <cstdio>
#include <cstring>
#include <cstdint>
#include "settings.h"
#include "saturn_backup.h"

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

static void test_defaults_are_unswapped(void)
{
    Settings s;
    s.swapButtons = true;
    settingsDefaults(&s);
    CHECK(!s.swapButtons);
}

static void test_record_round_trip(void)
{
    uint8_t buf[SETTINGS_SIZE];
    Settings out;

    Settings in;
    in.swapButtons = true;
    settingsPack(buf, &in);
    out.swapButtons = false;
    CHECK(settingsUnpack(buf, &out));
    CHECK(out.swapButtons);

    in.swapButtons = false;
    settingsPack(buf, &in);
    out.swapButtons = true;
    CHECK(settingsUnpack(buf, &out));
    CHECK(!out.swapButtons);
}

static void test_unpack_rejects_bad_magic(void)
{
    uint8_t buf[SETTINGS_SIZE];
    Settings in;
    in.swapButtons = true;
    settingsPack(buf, &in);
    buf[2] = 'X';

    Settings out;
    out.swapButtons = true;
    CHECK(!settingsUnpack(buf, &out));
    CHECK(out.swapButtons);
}

static void test_unpack_rejects_unknown_version(void)
{
    uint8_t buf[SETTINGS_SIZE];
    Settings in;
    in.swapButtons = true;
    settingsPack(buf, &in);
    buf[5] = SETTINGS_VER + 1;

    Settings out;
    out.swapButtons = true;
    CHECK(!settingsUnpack(buf, &out));
    CHECK(out.swapButtons);
}

static void test_pack_zeroes_reserved_bytes(void)
{
    uint8_t buf[SETTINGS_SIZE];
    memset(buf, 0xFF, sizeof(buf));

    Settings in;
    in.swapButtons = true;
    settingsPack(buf, &in);

    for (int i = 7; i < SETTINGS_SIZE; ++i) {
        CHECK_EQ(buf[i], 0);
    }
}

static void test_load_with_no_record_leaves_caller_defaults(void)
{
    stub_bup_reset();

    Settings s;
    s.swapButtons = true;
    CHECK(!settingsLoad(&s));
    CHECK(s.swapButtons);
}

static void test_store_then_load_round_trip(void)
{
    stub_bup_reset();

    Settings in;
    in.swapButtons = true;
    CHECK_EQ(settingsStore(&in), SAT_BUP_OK);

    Settings out;
    out.swapButtons = false;
    CHECK(settingsLoad(&out));
    CHECK(out.swapButtons);
}

static void test_store_overwrites_an_existing_record(void)
{
    stub_bup_reset();

    Settings in;
    in.swapButtons = true;
    CHECK_EQ(settingsStore(&in), SAT_BUP_OK);
    in.swapButtons = false;
    CHECK_EQ(settingsStore(&in), SAT_BUP_OK);

    Settings out;
    out.swapButtons = true;
    CHECK(settingsLoad(&out));
    CHECK(!out.swapButtons);
}

static void test_store_reports_a_missing_device(void)
{
    stub_bup_reset();
    stub_bup_set_device(SAT_BUP_INTERNAL, 0, 0, 0, 0);

    Settings in;
    in.swapButtons = true;
    CHECK_EQ(settingsStore(&in), SAT_BUP_ERR_NONE);
}

static void test_load_rejects_a_foreign_record(void)
{
    stub_bup_reset();

    uint8_t junk[SETTINGS_SIZE];
    memset(junk, 0x5A, sizeof(junk));
    stub_bup_add_file(SAT_BUP_INTERNAL, "AW_CFG", junk, SETTINGS_SIZE, 0);

    Settings s;
    s.swapButtons = true;
    CHECK(!settingsLoad(&s));
    CHECK(s.swapButtons);
}

static void test_default_layout_acts_on_a_and_c(void)
{
    bool jump = true, action = false;

    settingsMapFaceButtons(true, false, false, false, &jump, &action);
    CHECK(action);
    CHECK(!jump);

    settingsMapFaceButtons(false, false, true, false, &jump, &action);
    CHECK(action);
    CHECK(!jump);
}

static void test_default_layout_jumps_on_b(void)
{
    bool jump = false, action = true;
    settingsMapFaceButtons(false, true, false, false, &jump, &action);
    CHECK(jump);
    CHECK(!action);
}

static void test_swapped_layout_reverses_both(void)
{
    bool jump = false, action = false;

    settingsMapFaceButtons(true, false, false, true, &jump, &action);
    CHECK(jump);
    CHECK(!action);

    settingsMapFaceButtons(false, false, true, true, &jump, &action);
    CHECK(jump);
    CHECK(!action);

    settingsMapFaceButtons(false, true, false, true, &jump, &action);
    CHECK(action);
    CHECK(!jump);
}

static void test_nothing_held_maps_to_nothing(void)
{
    bool jump = true, action = true;
    settingsMapFaceButtons(false, false, false, false, &jump, &action);
    CHECK(!jump);
    CHECK(!action);

    jump = true;
    action = true;
    settingsMapFaceButtons(false, false, false, true, &jump, &action);
    CHECK(!jump);
    CHECK(!action);
}

static void test_both_actions_can_be_held_at_once(void)
{
    bool jump = false, action = false;
    settingsMapFaceButtons(true, true, false, false, &jump, &action);
    CHECK(jump);
    CHECK(action);
}

int main(void)
{
    test_defaults_are_unswapped();
    test_record_round_trip();
    test_unpack_rejects_bad_magic();
    test_unpack_rejects_unknown_version();
    test_pack_zeroes_reserved_bytes();
    test_load_with_no_record_leaves_caller_defaults();
    test_store_then_load_round_trip();
    test_store_overwrites_an_existing_record();
    test_store_reports_a_missing_device();
    test_load_rejects_a_foreign_record();
    test_default_layout_acts_on_a_and_c();
    test_default_layout_jumps_on_b();
    test_swapped_layout_reverses_both();
    test_nothing_held_maps_to_nothing();
    test_both_actions_can_be_held_at_once();

    if (g_fail == 0) {
        printf("all tests passed\n");
        return 0;
    }
    printf("%d check(s) failed\n", g_fail);
    return 1;
}
