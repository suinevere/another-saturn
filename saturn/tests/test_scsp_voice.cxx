/*----------------------
 | test_scsp_voice.cxx
 | Description: Host unit tests for scsp_voice.cxx. Built and run by
 |   run_tests.sh with the host g++, never by the Saturn makefile -- that
 |   globs src/ only, so tests/ is excluded automatically.
 | Author: suinevere
 ----------------------*/
#include <cstdio>
#include <cstdint>
#include "scsp_voice.h"

static int g_fail = 0;

#define CHECK_EQ(actual, expected)                                            \
    do {                                                                      \
        long long a_ = (long long)(actual);                                   \
        long long e_ = (long long)(expected);                                 \
        if (a_ != e_) {                                                       \
            g_fail++;                                                         \
            printf("FAIL %s:%d  %s\n  actual   = %lld (0x%llX)\n"             \
                   "  expected = %lld (0x%llX)\n",                            \
                   __FILE__, __LINE__, #actual, a_,                           \
                   (unsigned long long)a_, e_, (unsigned long long)e_);       \
        }                                                                     \
    } while (0)

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            g_fail++;                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                     \
    } while (0)

/* OCT is 4-bit signed; FNS is 10-bit UNSIGNED. Unpack for readability. */
static int pitchOct(uint16_t w) { int o = (w >> 11) & 0xF; return (o & 0x8) ? o - 16 : o; }
static int pitchFns(uint16_t w) { return w & 0x3FF; }

static void test_pitch_known_rates(void)
{
    /* 44100 is the SCSP's own rate: no shift, no fraction. */
    CHECK_EQ(scsp_voice_pitch(44100), 0x0000);

    /* Exact halvings land on FNS = 0 with OCT one lower each time. Getting
       FNS = 0 here is the whole point: the old calcPitch produced -512 for
       these, which is not representable in an unsigned field. */
    CHECK_EQ(scsp_voice_pitch(22050), 0x7800);   /* OCT -1, FNS 0 */
    CHECK_EQ(scsp_voice_pitch(11025), 0x7000);   /* OCT -2, FNS 0 */
    CHECK_EQ(scsp_voice_pitch(5512),  0x6800);   /* OCT -3, FNS 0 */

    /* Halfway between two octaves: FNS 512, the fraction at its midpoint. */
    CHECK_EQ(scsp_voice_pitch(33075), 0x7A00);   /* OCT -1, FNS 512 */

    /* Just under the top of an octave: FNS near full scale. */
    CHECK_EQ(scsp_voice_pitch(16537), 0x71FF);   /* OCT -2, FNS 511 */

    /* The engine's highest note: Amiga period 0x37, 7159092 / (0x37 * 2). */
    CHECK_EQ(scsp_voice_pitch(65082), 0x01E7);   /* OCT  0, FNS 487 */

    /* The engine's lowest note: period 0xFFF. */
    CHECK_EQ(scsp_voice_pitch(874), 0x5112);     /* OCT -6, FNS 274 */
}

static void test_pitch_msk10_overflow(void)
{
    /* Regression test for the one rate in the engine's entire range where the
       raw formula overflows: at 11024 Hz the fraction computes to exactly
       1024, and masking it to 10 bits turns it into 0 -- dropping a whole
       octave, a 50% pitch error. SGL's own PCM_MSK10 macro has this hole.
       The guard decrements the octave and recomputes instead. */
    CHECK_EQ(scsp_voice_pitch(11024), 0x7000);
}

static void test_pitch_fields_stay_in_range(void)
{
    /* Sweep the whole range the engine can ask for and prove neither field
       overflows its bits. FNS is UNSIGNED here: a negative value means the
       octave came out too low, which is exactly the bug the old calcPitch
       had, and it aliases to a wildly wrong pitch. */
    for (uint32_t rate = 874; rate <= 65082; rate++) {
        uint16_t w = scsp_voice_pitch(rate);
        int oct = pitchOct(w);
        int fns = pitchFns(w);
        if (oct < -8 || oct > 7 || fns < 0 || fns > 1023) {
            printf("FAIL rate %u -> 0x%04X OCT=%d FNS=%d out of range\n",
                   (unsigned)rate, w, oct, fns);
            g_fail++;
            return;
        }
    }
}

static void test_pitch_zero_is_safe(void)
{
    /* Never divide by zero, whatever the caller does. */
    CHECK_EQ(scsp_voice_pitch(0), 0x0000);
}

static void test_tl_table(void)
{
    /* Full engine volume is no attenuation. */
    CHECK_EQ(scsp_voice_tl(63), 0);

    /* TL is 0.375 dB per step, so halving the linear volume -- 6.02 dB --
       is 16 steps. This invariant is the whole point of the table. */
    CHECK_EQ(scsp_voice_tl(32), 16);
    CHECK_EQ(scsp_voice_tl(16), 32);
    CHECK_EQ(scsp_voice_tl(8),  48);
    CHECK_EQ(scsp_voice_tl(4),  64);
    CHECK_EQ(scsp_voice_tl(2),  80);
    CHECK_EQ(scsp_voice_tl(1),  96);

    /* Volume 0 is silence, not "very quiet". */
    CHECK_EQ(scsp_voice_tl(0), 255);

    /* Monotonic: louder input must never mean more attenuation. */
    for (uint8_t v = 1; v < 63; v++) {
        CHECK(scsp_voice_tl(v) >= scsp_voice_tl((uint8_t)(v + 1)));
    }

    /* The engine clamps volume to 0x3F (vm.cxx:690, sfxplayer.cxx:165) but
       be defensive: anything above 63 is full volume, not an overrun. */
    CHECK_EQ(scsp_voice_tl(64),  0);
    CHECK_EQ(scsp_voice_tl(255), 0);
}

static void test_upload_bytes(void)
{
    /* Both playChannel call sites build the same Amiga MOD shape: a one-shot
       part of len bytes followed by a loop part of loopLen bytes. The upload
       is therefore always the sum -- see vm.cxx:681 and sfxplayer.cxx:188. */
    CHECK_EQ(scsp_voice_upload_bytes(1000, 0),   1000);
    CHECK_EQ(scsp_voice_upload_bytes(1000, 500), 1500);

    /* Rounded up to even: the SCSP's RAM is 16-bit and the copy is done in
       16-bit units. */
    CHECK_EQ(scsp_voice_upload_bytes(999, 0),    1000);
    CHECK_EQ(scsp_voice_upload_bytes(999, 501),  1500);

    /* Both counts are uint16_t, so the sum must be computed in 32 bits or a
       large sample wraps to a tiny allocation and the copy runs off the end. */
    CHECK_EQ(scsp_voice_upload_bytes(60000, 60000), 120000);

    CHECK_EQ(scsp_voice_upload_bytes(0, 0), 0);
}

static void test_points_one_shot(void)
{
    /* vm.cxx:684-688 with loopLen 0: len bytes, no loop. */
    ScspVoicePoints p;
    CHECK_EQ(scsp_voice_points(0x20000, 1000, 0, 0, &p), 1);
    CHECK_EQ(p.sa,   0x20000);
    CHECK_EQ(p.lsa,  0);
    CHECK_EQ(p.lea,  999);      /* last sample, not one past it */
    CHECK_EQ(p.loop, 0);
}

static void test_points_looping(void)
{
    /* The looping shape: loopPos == len, so the one-shot intro is [0, len)
       and the sustained tail is [len, len + loopLen). This is the case the
       software mixer got wrong -- it restarted at loopPos scaled by 1/256 --
       and the reason this function is tested rather than eyeballed. */
    ScspVoicePoints p;
    CHECK_EQ(scsp_voice_points(0x20000, 1000, 1000, 500, &p), 1);
    CHECK_EQ(p.sa,   0x20000);
    CHECK_EQ(p.lsa,  1000);     /* loop starts where the intro ends */
    CHECK_EQ(p.lea,  1499);     /* last byte of the uploaded data */
    CHECK_EQ(p.loop, 1);
}

static void test_points_rejects_bad_input(void)
{
    ScspVoicePoints p;

    /* Nothing to play. */
    CHECK_EQ(scsp_voice_points(0x20000, 0, 0, 0, &p), 0);

    /* LEA past the end of what gets uploaded: the slot would read whatever
       follows the sample in sound RAM. */
    CHECK_EQ(scsp_voice_points(0x20000, 100, 200, 50, &p), 0);

    /* LEA beyond the 16-bit register. */
    CHECK_EQ(scsp_voice_points(0x20000, 60000, 60000, 60000, &p), 0);
}

/* Stand-in sample addresses. Only their identity matters to the cache; it
   never dereferences them. */
static const uint8_t *fakeData(unsigned n)
{
    static const uint8_t blob[64] = { 0 };
    return blob + (n & 63);
}

static void test_cache_miss_then_hit(void)
{
    ScspCache c;
    uint32_t off = 0;

    scsp_cache_init(&c, 0x20000, 0x80000);

    /* First sight of a sample: reserved, caller must copy. */
    CHECK_EQ(scsp_cache_acquire(&c, fakeData(1), 1000, 0, &off), SCSP_CACHE_MISS);
    CHECK_EQ(off, 0x20000);

    /* Second sight: already resident, no copy, same address. This is what
       makes note-ons free once a module's instruments are warm. */
    CHECK_EQ(scsp_cache_acquire(&c, fakeData(1), 1000, 0, &off), SCSP_CACHE_HIT);
    CHECK_EQ(off, 0x20000);

    /* A different sample gets the next slab, rounded to even. */
    CHECK_EQ(scsp_cache_acquire(&c, fakeData(2), 500, 0, &off), SCSP_CACHE_MISS);
    CHECK_EQ(off, 0x20000 + 1000);

    CHECK_EQ(scsp_cache_used_entries(&c), 2);
    CHECK_EQ(scsp_cache_used_bytes(&c), 1500);
}

static void test_cache_same_pointer_different_geometry_is_a_miss(void)
{
    /* The resource system recycles addresses, so a matching pointer with a
       different length is a different sample. The flush on invalidation is
       what makes this correct in general; this check is the cheap backstop. */
    ScspCache c;
    uint32_t off = 0;

    scsp_cache_init(&c, 0x20000, 0x80000);

    CHECK_EQ(scsp_cache_acquire(&c, fakeData(1), 1000, 0, &off), SCSP_CACHE_MISS);
    CHECK_EQ(scsp_cache_acquire(&c, fakeData(1), 2000, 0, &off), SCSP_CACHE_MISS);
    CHECK_EQ(off, 0x20000 + 1000);
}

static void test_cache_reset_forgets_everything(void)
{
    ScspCache c;
    uint32_t off = 0;

    scsp_cache_init(&c, 0x20000, 0x80000);
    CHECK_EQ(scsp_cache_acquire(&c, fakeData(1), 1000, 0, &off), SCSP_CACHE_MISS);

    scsp_cache_reset(&c);

    CHECK_EQ(scsp_cache_used_entries(&c), 0);
    CHECK_EQ(scsp_cache_used_bytes(&c), 0);
    CHECK_EQ(scsp_cache_acquire(&c, fakeData(1), 1000, 0, &off), SCSP_CACHE_MISS);
    CHECK_EQ(off, 0x20000);
}

static void test_cache_exhaustion_resets_and_reports_it(void)
{
    /* A deliberately tiny heap: 4096 bytes. */
    ScspCache c;
    uint32_t off = 0;

    scsp_cache_init(&c, 0x20000, 0x21000);

    CHECK_EQ(scsp_cache_acquire(&c, fakeData(1), 3000, 0, &off), SCSP_CACHE_MISS);
    CHECK_EQ(off, 0x20000);

    /* Does not fit in what is left, but does fit in an empty heap. The heap
       is flushed and the caller is told -- it MUST key the slots off before
       copying, because a sounding note is playing out of the memory that is
       about to be handed away. */
    CHECK_EQ(scsp_cache_acquire(&c, fakeData(2), 2000, 0, &off),
             SCSP_CACHE_MISS_AFTER_RESET);
    CHECK_EQ(off, 0x20000);
    CHECK_EQ(scsp_cache_used_entries(&c), 1);
}

static void test_cache_too_big_is_refused(void)
{
    ScspCache c;
    uint32_t off = 0;

    scsp_cache_init(&c, 0x20000, 0x21000);   /* 4096 bytes */

    /* Larger than the whole heap: no amount of flushing helps, so the note is
       skipped rather than scribbling past the end. */
    CHECK_EQ(scsp_cache_acquire(&c, fakeData(1), 5000, 0, &off), SCSP_CACHE_TOO_BIG);

    /* An empty sample is nothing to play. */
    CHECK_EQ(scsp_cache_acquire(&c, fakeData(2), 0, 0, &off), SCSP_CACHE_TOO_BIG);
}

static void test_cache_full_table_resets(void)
{
    /* 32 entries, then the 33rd distinct sample forces a reset. */
    ScspCache c;
    uint32_t off = 0;

    scsp_cache_init(&c, 0x20000, 0x80000);

    for (unsigned i = 0; i < SCSP_CACHE_ENTRIES; i++) {
        CHECK_EQ(scsp_cache_acquire(&c, fakeData(i), 16, 0, &off), SCSP_CACHE_MISS);
    }
    CHECK_EQ(scsp_cache_used_entries(&c), SCSP_CACHE_ENTRIES);

    CHECK_EQ(scsp_cache_acquire(&c, fakeData(SCSP_CACHE_ENTRIES), 16, 0, &off),
             SCSP_CACHE_MISS_AFTER_RESET);
    CHECK_EQ(scsp_cache_used_entries(&c), 1);
    CHECK_EQ(off, 0x20000);
}

int main(void)
{
    test_pitch_known_rates();
    test_pitch_msk10_overflow();
    test_pitch_fields_stay_in_range();
    test_pitch_zero_is_safe();
    test_tl_table();
    test_upload_bytes();
    test_points_one_shot();
    test_points_looping();
    test_points_rejects_bad_input();
    test_cache_miss_then_hit();
    test_cache_same_pointer_different_geometry_is_a_miss();
    test_cache_reset_forgets_everything();
    test_cache_exhaustion_resets_and_reports_it();
    test_cache_too_big_is_refused();
    test_cache_full_table_resets();

    if (g_fail == 0) {
        printf("all tests passed\n");
        return 0;
    }

    printf("%d check(s) failed\n", g_fail);
    return 1;
}
