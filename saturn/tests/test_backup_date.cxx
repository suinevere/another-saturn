/*----------------------
 | test_backup_date.cxx
 | Description: Host unit tests for sat_bup_date_split, the one piece of
 |   saturn_backup that is pure arithmetic and can run off-target. The rest of
 |   the file talks to the BIOS and is verified on hardware only.
 | Author: suinevere
 | Dependencies: saturn_backup.h
 ----------------------*/
#include <cstdio>
#include <cstdint>
#include "saturn_backup.h"

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

/*----------------------
 | packed
 | Description: Builds a BUP date word from calendar fields, mirroring the
 |   packing sat_bup_date_split must invert.
 | Author: suinevere
 | Params: year, month, day, hour, min -- calendar fields
 | Returns: the packed word
 ----------------------*/
static uint32_t packed(int year, int month, int day, int hour, int min)
{
    static const int cum[12] = {0,31,59,90,120,151,181,212,243,273,304,334};
    int y = year - 1980;
    int days = y * 365 + (y + 3) / 4 + cum[month - 1] + (day - 1);
    if (month > 2 && (year % 4) == 0) {
        days += 1;
    }
    return (uint32_t)days * 1440u + (uint32_t)hour * 60u + (uint32_t)min;
}

/*----------------------
 | test_epoch
 | Description: Date word 0 must split back to the 1980-01-01 00:00 epoch.
 | Author: suinevere
 | Returns: N/A
 ----------------------*/
static void test_epoch(void)
{
    int mo = -1, d = -1, h = -1, mi = -1;
    sat_bup_date_split(0, &mo, &d, &h, &mi);
    CHECK_EQ(mo, 1);
    CHECK_EQ(d, 1);
    CHECK_EQ(h, 0);
    CHECK_EQ(mi, 0);
}

/*----------------------
 | test_known_stamp
 | Description: An ordinary date/time round-trips through pack then split.
 | Author: suinevere
 | Returns: N/A
 ----------------------*/
static void test_known_stamp(void)
{
    int mo = 0, d = 0, h = 0, mi = 0;
    sat_bup_date_split(packed(2026, 8, 1, 21, 14), &mo, &d, &h, &mi);
    CHECK_EQ(mo, 8);
    CHECK_EQ(d, 1);
    CHECK_EQ(h, 21);
    CHECK_EQ(mi, 14);
}

/*----------------------
 | test_leap_day
 | Description: February 29 of a leap year must split back correctly.
 | Author: suinevere
 | Returns: N/A
 ----------------------*/
static void test_leap_day(void)
{
    int mo = 0, d = 0, h = 0, mi = 0;
    sat_bup_date_split(packed(2024, 2, 29, 12, 0), &mo, &d, &h, &mi);
    CHECK_EQ(mo, 2);
    CHECK_EQ(d, 29);
    CHECK_EQ(h, 12);
}

/*----------------------
 | test_null_outputs_are_safe
 | Description: Passing NULL for any output pointer must not crash.
 | Author: suinevere
 | Returns: N/A
 ----------------------*/
static void test_null_outputs_are_safe(void)
{
    sat_bup_date_split(packed(2026, 8, 1, 21, 14), 0, 0, 0, 0);
}

/*----------------------
 | main
 | Description: Runs the sat_bup_date_split suite and reports pass/fail.
 | Author: suinevere
 | Returns: 0 on success, 1 if any check failed
 ----------------------*/
int main(void)
{
    test_epoch();
    test_known_stamp();
    test_leap_day();
    test_null_outputs_are_safe();

    if (g_fail == 0) {
        printf("all tests passed\n");
        return 0;
    }
    printf("%d check(s) failed\n", g_fail);
    return 1;
}
