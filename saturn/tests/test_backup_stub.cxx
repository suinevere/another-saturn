/*----------------------
 | test_backup_stub.cxx
 | Description: Host unit tests for stub_saturn_backup.cxx's error-path
 |   behaviour -- the parts of the sat_bup_* contract that the stub can prove
 |   on host. sat_bup_map_error's BUP_* code mapping and sat_bup_dir's
 |   negative/zero/positive BUP_Dir handling live only in saturn_backup.cxx,
 |   which talks to the BIOS and cannot run here; those are not covered by
 |   this suite and are verified on hardware only.
 | Author: suinevere
 | Dependencies: saturn_backup.h
 ----------------------*/
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "saturn_backup.h"

/* stub_bup_* control surface, defined in stub_saturn_backup.cxx with plain
   C++ linkage (not wrapped in extern "C" there), so declared the same way
   here rather than through saturn_backup.h, which only covers sat_bup_*. */
void stub_bup_reset(void);
void stub_bup_set_device(uint32_t device, int present, int formatted,
                         int writeProtected, uint32_t freeBytes);
void stub_bup_add_file(uint32_t device, const char *name, const void *data,
                       int32_t size, uint32_t date);

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
 | test_read_refuses_oversized_file
 | Description: A stored file bigger than dst must be refused, not truncated.
 | Author: suinevere
 | Returns: N/A
 ----------------------*/
static void test_read_refuses_oversized_file(void)
{
    stub_bup_reset();
    uint8_t stored[100];
    memset(stored, 0xAB, sizeof(stored));
    stub_bup_add_file(SAT_BUP_INTERNAL, "AW_SAVE1", stored, sizeof(stored), 0);

    uint8_t dst[50];
    int rc = sat_bup_read(SAT_BUP_INTERNAL, "AW_SAVE1", dst, sizeof(dst));
    CHECK_EQ(rc, SAT_BUP_ERR_BROKEN);
}

/*----------------------
 | test_read_exact_size_succeeds
 | Description: A stored file that exactly fits dst must still read cleanly.
 | Author: suinevere
 | Returns: N/A
 ----------------------*/
static void test_read_exact_size_succeeds(void)
{
    stub_bup_reset();
    uint8_t stored[50];
    memset(stored, 0xCD, sizeof(stored));
    stub_bup_add_file(SAT_BUP_INTERNAL, "AW_SAVE1", stored, sizeof(stored), 0);

    uint8_t dst[50];
    memset(dst, 0, sizeof(dst));
    int rc = sat_bup_read(SAT_BUP_INTERNAL, "AW_SAVE1", dst, sizeof(dst));
    CHECK_EQ(rc, SAT_BUP_OK);
    CHECK_EQ(memcmp(dst, stored, sizeof(stored)), 0);
}

/*----------------------
 | test_write_refuses_existing_without_overwrite
 | Description: Writing over an existing file with overwrite=0 must report
 |   SAT_BUP_ERR_EXISTS, not silently succeed and not claim no space.
 | Author: suinevere
 | Returns: N/A
 ----------------------*/
static void test_write_refuses_existing_without_overwrite(void)
{
    stub_bup_reset();
    uint8_t data[16];
    memset(data, 0x11, sizeof(data));

    int rc = sat_bup_write(SAT_BUP_INTERNAL, "AW_SAVE1", "c", data,
                           sizeof(data), 0);
    CHECK_EQ(rc, SAT_BUP_OK);

    rc = sat_bup_write(SAT_BUP_INTERNAL, "AW_SAVE1", "c", data, sizeof(data),
                       0);
    CHECK_EQ(rc, SAT_BUP_ERR_EXISTS);
}

/*----------------------
 | test_write_allows_existing_with_overwrite
 | Description: Writing over an existing file with overwrite!=0 must succeed.
 | Author: suinevere
 | Returns: N/A
 ----------------------*/
static void test_write_allows_existing_with_overwrite(void)
{
    stub_bup_reset();
    uint8_t data[16];
    memset(data, 0x22, sizeof(data));

    int rc = sat_bup_write(SAT_BUP_INTERNAL, "AW_SAVE1", "c", data,
                           sizeof(data), 0);
    CHECK_EQ(rc, SAT_BUP_OK);

    rc = sat_bup_write(SAT_BUP_INTERNAL, "AW_SAVE1", "c", data, sizeof(data),
                       1);
    CHECK_EQ(rc, SAT_BUP_OK);
}

/*----------------------
 | test_write_refuses_over_stub_capacity
 | Description: A write bigger than the stub's fixed-size file buffer must be
 |   refused with SAT_BUP_ERR_NO_SPACE rather than overrunning it. 4097 must
 |   stay one byte above stub_saturn_backup.cxx's STUB_MAX_BYTES (4096).
 | Author: suinevere
 | Returns: N/A
 ----------------------*/
static void test_write_refuses_over_stub_capacity(void)
{
    stub_bup_reset();
    stub_bup_set_device(SAT_BUP_INTERNAL, 1, 1, 0, 1u << 20);

    static uint8_t big[4097];
    memset(big, 0x33, sizeof(big));

    int rc = sat_bup_write(SAT_BUP_INTERNAL, "AW_SAVE1", "c", big,
                           sizeof(big), 0);
    CHECK_EQ(rc, SAT_BUP_ERR_NO_SPACE);
}

/*----------------------
 | main
 | Description: Runs the stub error-path suite and reports pass/fail.
 | Author: suinevere
 | Returns: 0 on success, 1 if any check failed
 ----------------------*/
int main(void)
{
    test_read_refuses_oversized_file();
    test_read_exact_size_succeeds();
    test_write_refuses_existing_without_overwrite();
    test_write_allows_existing_with_overwrite();
    test_write_refuses_over_stub_capacity();

    if (g_fail == 0) {
        printf("all tests passed\n");
        return 0;
    }
    printf("%d check(s) failed\n", g_fail);
    return 1;
}
