# SCSP Hardware Mixing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cut Another-Saturn's audio latency from ~279 ms to ~17 ms by moving mixing off the SH-2 and onto the SCSP's own slot hardware.

**Architecture:** `Mixer` stops mixing and becomes a shim that programs four SCSP slots directly — hardware loop points, hardware pitch, hardware level. Sample data is copied into sound RAM behind a pointer-keyed cache that is flushed whenever the resource system recycles its memory block. SEGA's LIBPCM stream driver, the work-RAM ring, and `Mixer::mix` are all deleted.

**Tech Stack:** SH-2 cross build via SaturnRingLib (`saturn/compile.bat`), SGL headers for SCSP addresses, host `g++` (MSYS2, `/c/msys64/mingw64/bin/g++`) for unit tests of the pure logic, Mednafen and real Saturn hardware for verification.

**Spec:** `docs/superpowers/specs/2026-07-30-scsp-hardware-mixing-design.md`

## Global Constraints

- **New C++ files use `.cxx`, new C files use `.c`.** `saturn/makefile` derives object names with `$(SOURCES:.c=.o)` then `:.cxx=.o` and defines pattern rules only for `%.o : %.c` and `%.o : %.cxx`. A `.cpp` file is silently dropped from the link with no error.
- **The makefile globs `find src/ -name '*.cxx'`** from `saturn/`. Anything under `saturn/tests/` is therefore excluded from the target build automatically. Do not move tests into `src/`.
- **`saturn/src/system/scsp_voice.{h,cxx}` must not include `<srl.hpp>`, any SGL header, or any engine header.** It is compiled twice — once for SH-2, once for the host test runner — and must depend on nothing but `<stdint.h>`.
- **Only three files outside the audio layer may be touched:** `saturn/src/mixer.cxx`, `saturn/src/mixer.h`, `saturn/src/resource.cxx`. `sfxplayer.cxx`, `vm.cxx`, `bank.cxx` and `saturn_cdfile.cxx` stay as they are.
- **The save format must not change.** `MixerChannel` and `MixerChunk` keep every field and `Mixer::saveOrLoad` keeps its entry list byte for byte (`engine.cxx:140`, `engine.cxx:174`).
- **Sound RAM sample heap is `0x020000` to `0x080000`** (384 KB). Sound RAM base is `SoundRAM` = `0x25A00000`. The low 128 KB is left alone.
- **SCSP slots 0–3**, one per engine channel. Slot *n* registers at `0x25B00000 + n*0x20`; common block at `0x25B00400`. All register access is 16-bit.
- **Follow the existing comment style.** Every new file and every non-obvious constant gets a `/*---------------------- | Name | Description: ... | Author: suinevere ----------------------*/` block, matching `saturn_audio.cxx`.
- **Commit every task.** Stage the specific files named in the task; never `git add -A`.

## File Structure

| File | Responsibility |
|---|---|
| `docs/scsp-registers.md` | New. The verified register map. The reference every later task programs against. |
| `saturn/src/system/scsp_voice.h` | New. Pure interface: pitch word, TL table, loop-point derivation, heap/cache bookkeeping. No hardware, no engine types. |
| `saturn/src/system/scsp_voice.cxx` | New. Implementation of the above. Host-testable. |
| `saturn/src/system/saturn_scsp.h` | New. C interface for the hardware layer, consumed by `mixer.cxx` and `resource.cxx`. |
| `saturn/src/system/saturn_scsp.cxx` | New. Register writes, sound RAM copies, 68000 stand-down, debug counters. Includes `<srl.hpp>`. |
| `saturn/tests/test_scsp_voice.cxx` | New. Host unit tests. |
| `saturn/tests/run_tests.sh` | New. Host test runner. |
| `saturn/src/mixer.cxx` | Modified. `mix`/`mixCallback`/`addclamp` deleted; the four control methods become `sat_scsp_*` calls. |
| `saturn/src/mixer.h` | Modified. `mix`/`mixCallback` declarations removed. Structs untouched. |
| `saturn/src/resource.cxx` | Modified. Two `sat_scsp_flush_samples()` calls. |
| `saturn/src/system/saturn_audio.cxx` | Modified. Reduced to timers plus `sat_audio_update`. |
| `saturn/src/system/saturn_audio.h` | Modified. `sat_audio_vblank` removed. |
| `saturn/src/system/saturn_system.cxx` | Modified. `startAudio` drops the callback. |
| `saturn/src/system/saturn_platform.cxx` | Modified. `sat_audio_vblank()` call removed from `onVblank`. |

The split between `scsp_voice` (pure) and `saturn_scsp` (hardware) is the one refinement over the spec, which described a single file. It exists so the arithmetic that has historically been wrong here — loop points, pitch — can be tested on the host instead of by ear.

---

### Task 1: Verify and document the SCSP register map

No code. This task exists because a wrong bit position produces silence with no error, and every later task programs against this document.

**Files:**
- Create: `docs/scsp-registers.md`

**Interfaces:**
- Consumes: nothing.
- Produces: a verified register map. Later tasks cite it rather than re-deriving it.

- [ ] **Step 1: Check the map below against an external reference**

The map below is the starting point, not the answer. Verify every row against at least one of: Mednafen's `mednafen/ss/scsp.cpp` and `scsp.h` (the SCSP core is shared with the Saturn driver), the SCSP section of the Sega Saturn hardware manual, or the CyberWarriorX / Yabause `scsp.c`. Note which reference you used.

Only one address in this repository corroborates anything: `sega_snd.h:142` defines `SND_ADR_INTR_RESET` as `0x25b0042e`, which is `MCIRE` at common offset `0x2E`. That fixes the common-block base at `0x25B00400` and nothing else.

Slot *n* base = `0x25B00000 + n * 0x20`. All 16-bit.

| Offset | Bits | Field | Meaning |
|---|---|---|---|
| `+0x00` | 12 | `KYONEX` | Write 1 to commit pending `KYONB` across all slots |
| `+0x00` | 11 | `KYONB` | Key on/off request for this slot |
| `+0x00` | 10–9 | `SBCTL` | Sound source inversion. 0 = none |
| `+0x00` | 8–7 | `SSCTL` | Sound source. 0 = sound RAM |
| `+0x00` | 6–5 | `LPCTL` | 0 = off, 1 = forward loop, 2 = reverse, 3 = alternating |
| `+0x00` | 4 | `PCM8B` | 1 = 8-bit signed samples |
| `+0x00` | 3–0 | `SA[19:16]` | Start address, high nibble |
| `+0x02` | 15–0 | `SA[15:0]` | Start address, low word (byte offset in sound RAM) |
| `+0x04` | 15–0 | `LSA` | Loop start, in samples from `SA` |
| `+0x06` | 15–0 | `LEA` | Loop end, in samples from `SA` |
| `+0x08` | 15–11 | `D2R` | Decay 2 rate |
| `+0x08` | 10–6 | `D1R` | Decay 1 rate |
| `+0x08` | 5 | `EGHOLD` | Envelope hold |
| `+0x08` | 4–0 | `AR` | Attack rate |
| `+0x0A` | 14 | `LPSLNK` | Loop-start link |
| `+0x0A` | 13–10 | `KRS` | Key rate scaling |
| `+0x0A` | 9–5 | `DL` | Decay level |
| `+0x0A` | 4–0 | `RR` | Release rate |
| `+0x0C` | 9 | `STWINH` | Stack write inhibit |
| `+0x0C` | 8 | `SDIR` | Sound direct |
| `+0x0C` | 7–0 | `TL` | Total level. 0 = loudest, 255 = silent, 0.375 dB per step |
| `+0x10` | 14–11 | `OCT` | Octave, 4-bit signed, −8..+7 |
| `+0x10` | 9–0 | `FNS` | Frequency number switch, 10 bits |
| `+0x16` | 15–13 | `DISDL` | Direct send level, 0–7 |
| `+0x16` | 12–8 | `DIPAN` | Direct pan, 0 = centre |

Common block:

| Address | Field | Meaning |
|---|---|---|
| `0x25B00400` | `MVOL` bits 3–0 | Master volume, 0–15 |
| `0x25B0042E` | `MCIRE` | Main-CPU interrupt reset. Corroborated by `sega_snd.h:142` |

- [ ] **Step 2: Resolve the FNS sign question in the document**

This is the important one. `calcPitch()` (`saturn/src/system/saturn_audio.cxx:365`) produces a **negative** FNS for every rate below 44100 Hz. Compiled and swept over the engine's full range (874–65082 Hz, from Amiga periods `0x37`–`0xFFF`), it yields FNS in `[-512, 487]` and OCT in `[-5, 0]`.

Those values are only correct under:

```
playback_rate = (1 + FNS_signed / 1024) * 2^OCT * 44100
```

Under that model the worst-case error across the range is 0.13%. Under an unsigned-FNS model, `calcPitch(22050)` returns OCT=0, FNS=0x200 and would play at 66150 Hz — a fifth sharp instead of an octave flat.

Record in the document which model your reference supports. If the reference is ambiguous, write that down; Task 5 settles it empirically and you will come back and fill this in.

Note that `calcPitch` is currently used **only** on the `MODE_BUFFERS` fallback path, which `saturn_audio.cxx:113` says normally never runs. It is effectively unverified in the shipping build. Do not assume it is correct because the game currently sounds right.

- [ ] **Step 3: Commit**

```bash
git add docs/scsp-registers.md
git commit -m "Document the SCSP register map, verified against a reference

Every later step of the hardware-mixing work programs against this. A wrong
bit position here is silence with no error to catch it, so it is written
down and checked before any code depends on it."
```

---

### Task 2: Host test harness, pitch word and TL table

**Files:**
- Create: `saturn/src/system/scsp_voice.h`
- Create: `saturn/src/system/scsp_voice.cxx`
- Create: `saturn/tests/test_scsp_voice.cxx`
- Create: `saturn/tests/run_tests.sh`

**Interfaces:**
- Consumes: `docs/scsp-registers.md` from Task 1.
- Produces:
  - `uint16_t scsp_voice_pitch(uint32_t sampleRate)` — packed OCT|FNS word for `+0x10`.
  - `uint8_t scsp_voice_tl(uint8_t volume)` — engine volume 0–63 to `TL` 0–255.
  - `saturn/tests/run_tests.sh` — the host test command every later task re-runs.

- [ ] **Step 1: Write the test runner**

Create `saturn/tests/run_tests.sh`:

```sh
#!/bin/sh
# Host unit tests for the pure SCSP voice maths. Nothing here touches hardware
# or SRL, which is the whole point: this is the arithmetic that has historically
# been wrong in this backend and it is cheap to test off-target.
set -e
cd "$(dirname "$0")"
g++ -std=c++11 -Wall -Wextra -Werror -O1 -g \
    -I../src/system \
    -o run_tests test_scsp_voice.cxx ../src/system/scsp_voice.cxx
./run_tests
```

Make it executable: `chmod +x saturn/tests/run_tests.sh`

- [ ] **Step 2: Write the failing test**

Create `saturn/tests/test_scsp_voice.cxx`:

```cpp
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

/* OCT is 4-bit signed, FNS is 10-bit signed. Unpack for readability. */
static int pitchOct(uint16_t w) { int o = (w >> 11) & 0xF; return (o & 0x8) ? o - 16 : o; }
static int pitchFns(uint16_t w) { int f = w & 0x3FF;       return (f & 0x200) ? f - 1024 : f; }

static void test_pitch_known_rates(void)
{
    /* 44100 is the SCSP's own rate: no shift, no fraction. */
    CHECK_EQ(scsp_voice_pitch(44100), 0x0000);

    /* Exact powers-of-two below it land on FNS = -512, OCT one lower each time. */
    CHECK_EQ(scsp_voice_pitch(22050), 0x0200);
    CHECK_EQ(scsp_voice_pitch(11025), 0x7A00);
    CHECK_EQ(scsp_voice_pitch(5512),  0x7200);

    /* Three-quarter rate: FNS = -256, same octave. */
    CHECK_EQ(scsp_voice_pitch(33075), 0x0300);
    CHECK_EQ(scsp_voice_pitch(16537), 0x7B00);

    /* Above 44100 the fraction goes positive. 65082 Hz is the engine's
       highest note: Amiga period 0x37, 7159092 / (0x37 * 2). */
    CHECK_EQ(scsp_voice_pitch(65082), 0x01E7);

    /* The engine's lowest note: period 0xFFF. */
    CHECK_EQ(scsp_voice_pitch(874), 0x5A8A);
}

static void test_pitch_fields_stay_in_range(void)
{
    /* Sweep the whole range the engine can ask for and prove neither field
       overflows its bits. OCT is 4-bit signed and FNS 10-bit signed, so a
       value outside these ranges silently aliases to a wrong pitch. */
    for (uint32_t rate = 874; rate <= 65082; rate++) {
        uint16_t w = scsp_voice_pitch(rate);
        int oct = pitchOct(w);
        int fns = pitchFns(w);
        if (oct < -8 || oct > 7 || fns < -512 || fns > 511) {
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

int main(void)
{
    test_pitch_known_rates();
    test_pitch_fields_stay_in_range();
    test_pitch_zero_is_safe();
    test_tl_table();

    if (g_fail == 0) {
        printf("all tests passed\n");
        return 0;
    }

    printf("%d check(s) failed\n", g_fail);
    return 1;
}
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `sh saturn/tests/run_tests.sh`
Expected: FAIL — `scsp_voice.h: No such file or directory`

- [ ] **Step 4: Write the header**

Create `saturn/src/system/scsp_voice.h`:

```c
/*----------------------
 | scsp_voice.h
 | Description: The arithmetic behind an SCSP voice, with no hardware in it.
 |
 |   Deliberately free of <srl.hpp>, SGL and every engine header: this file is
 |   compiled twice, once for the SH-2 and once by saturn/tests/run_tests.sh
 |   with the host g++. Keeping it to <stdint.h> is what makes the loop-point
 |   and pitch maths testable off-target, which matters because both have been
 |   wrong here before -- see the loop-restart note in the mixer's history.
 |
 |   The hardware half lives in saturn_scsp.cxx.
 |
 |   Register meanings: docs/scsp-registers.md
 | Author: suinevere
 | Dependencies: stdint.h
 ----------------------*/
#ifndef SCSP_VOICE_H
#define SCSP_VOICE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | scsp_voice_pitch
 | Description: Packs a playback rate into the SCSP's OCT/FNS word, ready for
 |   slot register +0x10.
 |
 |   The model this assumes is
 |       rate = (1 + FNS/1024) * 2^OCT * 44100
 |   with FNS read as SIGNED 10-bit. Every rate below 44100 produces a negative
 |   FNS, so under an unsigned reading this function is wrong by up to a fifth
 |   -- 22050 would play at 66150. See docs/scsp-registers.md.
 |
 |   Worst-case error across the engine's range (874..65082 Hz) is 0.13%.
 | Author: suinevere
 | Params: sampleRate -- desired playback rate in Hz. 0 returns 0.
 ----------------------*/
uint16_t scsp_voice_pitch(uint32_t sampleRate);

/*----------------------
 | scsp_voice_tl
 | Description: Maps the engine's linear volume onto the SCSP's logarithmic TL
 |   attenuation, for slot register +0x0C bits 7-0.
 |
 |   The engine works in 0..63 (vm.cxx clamps with MIN(vol, 0x3F)); TL is 0..255
 |   where 0 is loudest and each step is 0.375 dB. Halving the linear volume is
 |   therefore 16 TL steps.
 | Author: suinevere
 | Params: volume -- 0..63. 0 means silence. Above 63 is treated as 63.
 ----------------------*/
uint8_t scsp_voice_tl(uint8_t volume);

#ifdef __cplusplus
}
#endif
#endif /* SCSP_VOICE_H */
```

- [ ] **Step 5: Write the implementation**

Create `saturn/src/system/scsp_voice.cxx`:

```c
/*----------------------
 | scsp_voice.cxx
 | Description: Pure SCSP voice arithmetic. See scsp_voice.h for why this file
 |   has no hardware and no dependencies beyond stdint.h.
 | Author: suinevere
 | Dependencies: scsp_voice.h
 ----------------------*/
#include "scsp_voice.h"

/*----------------------
 | SCSP_BASE_RATE
 | Description: What a slot plays at with OCT = 0 and FNS = 0. Everything here
 |   is expressed relative to it.
 | Author: suinevere
 ----------------------*/
#define SCSP_BASE_RATE 44100

uint16_t scsp_voice_pitch(uint32_t sampleRate)
{
    if (sampleRate == 0)
    {
        return 0;
    }

    /* How many halvings of the base rate it takes to get at or below the
       requested rate. The +1 keeps the division from returning a ratio that
       rounds the octave up at exact boundaries.

       This reproduces SGL's PCM_CALC_* macros rather than calling them: those
       index SRL's LogTable, a private member of SRL::Sound::Pcm. The table is
       just floor(log2(n)), cheaper to recompute than to duplicate. */
    const uint32_t ratio = (uint32_t)SCSP_BASE_RATE / (sampleRate + 1u);

    int32_t octave = 0;
    while ((ratio >> (octave + 1)) != 0)
    {
        octave++;
    }

    const int32_t shiftFreq = (int32_t)SCSP_BASE_RATE >> octave;

    /* Signed, and always negative below the shifted rate. C's division
       truncates toward zero, which is what keeps this at or above -512: the
       octave search guarantees sampleRate >= shiftFreq / 2, and truncation
       never rounds the result further from zero. Do not "fix" this into a
       floored division -- that would produce -513 at exact boundaries and
       alias to +511, playing the note a fifth sharp instead of an octave
       flat. */
    const int32_t fns = (((int32_t)sampleRate - shiftFreq) << 10) / shiftFreq;

    /* OCT in the register is the negation: octave counts halvings, OCT
       expresses them as a signed power of two. */
    return (uint16_t)((((-octave) & 0x0F) << 11) | (fns & 0x03FF));
}

/*----------------------
 | g_tlTable
 | Description: Engine volume 0..63 to SCSP TL, precomputed.
 |
 |   TL[v] = round(-20 * log10(v / 63) / 0.375)
 |
 |   A table rather than a computation because the SH-2 has no FPU and this is
 |   on the note-on path. The shape is easy to check by eye: every halving of
 |   v adds 16, since 6.02 dB / 0.375 dB is 16 steps.
 | Author: suinevere
 ----------------------*/
static const uint8_t g_tlTable[64] = {
    255,  96,  80,  71,  64,  59,  54,  51,   /*  0..7  */
     48,  45,  43,  40,  38,  37,  35,  33,   /*  8..15 */
     32,  30,  29,  28,  27,  25,  24,  23,   /* 16..23 */
     22,  21,  20,  20,  19,  18,  17,  16,   /* 24..31 */
     16,  15,  14,  14,  13,  12,  12,  11,   /* 32..39 */
     11,  10,   9,   9,   8,   8,   7,   7,   /* 40..47 */
      6,   6,   5,   5,   4,   4,   4,   3,   /* 48..55 */
      3,   2,   2,   2,   1,   1,   0,   0    /* 56..63 */
};

uint8_t scsp_voice_tl(uint8_t volume)
{
    if (volume > 63)
    {
        volume = 63;
    }

    return g_tlTable[volume];
}
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `sh saturn/tests/run_tests.sh`
Expected: PASS — `all tests passed`

If `test_pitch_known_rates` fails, the expected words in the test are the authority: they were produced by compiling the existing `calcPitch` with the host g++ and sweeping it. A mismatch means the port of the function changed its behaviour.

- [ ] **Step 7: Verify the Saturn build still compiles**

Run: `cd saturn && ./compile.bat debug`
Expected: builds clean. `scsp_voice.cxx` is now in the target build (the makefile globs `src/`) but nothing calls it yet.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/system/scsp_voice.h saturn/src/system/scsp_voice.cxx \
        saturn/tests/test_scsp_voice.cxx saturn/tests/run_tests.sh
git commit -m "Host-testable SCSP pitch word and volume table

calcPitch has only ever run on the slPCMOn fallback path, which the backend
says normally never runs, so it has been shipping unverified. It moves here
where a host g++ can sweep it across the engine's whole frequency range and
prove OCT and FNS stay inside their fields."
```

---

### Task 3: Loop-point derivation

**Files:**
- Modify: `saturn/src/system/scsp_voice.h`
- Modify: `saturn/src/system/scsp_voice.cxx`
- Modify: `saturn/tests/test_scsp_voice.cxx`

**Interfaces:**
- Consumes: `scsp_voice.h` from Task 2.
- Produces:
  - `struct ScspVoicePoints { uint32_t sa; uint16_t lsa; uint16_t lea; uint8_t loop; }`
  - `uint32_t scsp_voice_upload_bytes(uint16_t len, uint16_t loopLen)`
  - `int scsp_voice_points(uint32_t base, uint16_t len, uint16_t loopPos, uint16_t loopLen, ScspVoicePoints *out)` — returns 1 on success, 0 if the note must be skipped.

- [ ] **Step 1: Write the failing test**

Append to `saturn/tests/test_scsp_voice.cxx`, before `main`:

```cpp
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
```

Add the four calls to `main`:

```cpp
    test_upload_bytes();
    test_points_one_shot();
    test_points_looping();
    test_points_rejects_bad_input();
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `sh saturn/tests/run_tests.sh`
Expected: FAIL — `'ScspVoicePoints' was not declared in this scope`

- [ ] **Step 3: Add the interface**

Append to `saturn/src/system/scsp_voice.h`, inside the `extern "C"` block:

```c
/*----------------------
 | ScspVoicePoints
 | Description: The address fields of a slot: registers +0x00 (LPCTL and the
 |   high nibble of SA), +0x02 (SA low), +0x04 (LSA) and +0x06 (LEA).
 |
 |   lea is the LAST sample played, not one past it, matching the hardware.
 | Author: suinevere
 ----------------------*/
typedef struct
{
    uint32_t sa;    /* byte offset into sound RAM */
    uint16_t lsa;   /* loop start, samples from sa */
    uint16_t lea;   /* loop end, samples from sa, inclusive */
    uint8_t  loop;  /* 1 = forward loop, 0 = one-shot */
} ScspVoicePoints;

/*----------------------
 | scsp_voice_upload_bytes
 | Description: How many bytes of sound RAM a sample needs.
 |
 |   Both playChannel call sites (vm.cxx:681, sfxplayer.cxx:188) build the same
 |   Amiga MOD shape: a one-shot part of len bytes immediately followed by a
 |   loop part of loopLen bytes, so the total is the sum. Rounded up to even
 |   because the SCSP's RAM is 16-bit.
 |
 |   Returns uint32_t on purpose: both inputs are uint16_t and their sum does
 |   not fit one.
 | Author: suinevere
 ----------------------*/
uint32_t scsp_voice_upload_bytes(uint16_t len, uint16_t loopLen);

/*----------------------
 | scsp_voice_points
 | Description: Derives a slot's address fields from a MixerChunk's geometry.
 |
 |   With loopLen non-zero the SCSP plays sa..lea then jumps back to lsa, so
 |   the intro sounds once and the tail sustains -- exactly what Mixer::mix
 |   used to do by hand. With loopLen zero the slot stops at lea by itself and
 |   nothing has to poll for the end.
 |
 |   Returns 0 when the note must be skipped rather than played wrong: empty
 |   data, a loop end past the uploaded bytes, or a loop end that will not fit
 |   the 16-bit register.
 | Author: suinevere
 | Params: base -- sound RAM byte offset the sample was uploaded to
 |         out  -- filled only when the return value is 1
 ----------------------*/
int scsp_voice_points(uint32_t base, uint16_t len, uint16_t loopPos,
                      uint16_t loopLen, ScspVoicePoints *out);
```

- [ ] **Step 4: Write the implementation**

Append to `saturn/src/system/scsp_voice.cxx`:

```c
uint32_t scsp_voice_upload_bytes(uint16_t len, uint16_t loopLen)
{
    /* 32-bit on purpose: len and loopLen are both uint16_t and a large
       instrument sums past 65535. */
    uint32_t total = (uint32_t)len + (uint32_t)loopLen;

    return (total + 1u) & ~1u;
}

int scsp_voice_points(uint32_t base, uint16_t len, uint16_t loopPos,
                      uint16_t loopLen, ScspVoicePoints *out)
{
    const uint32_t total = (uint32_t)len + (uint32_t)loopLen;

    uint32_t lsa;
    uint32_t lea;
    uint8_t  loop;

    if (out == 0 || total == 0)
    {
        return 0;
    }

    if (loopLen != 0)
    {
        lsa  = loopPos;
        lea  = (uint32_t)loopPos + (uint32_t)loopLen - 1u;
        loop = 1;
    }
    else
    {
        lsa  = 0;
        lea  = (uint32_t)len - 1u;
        loop = 0;
    }

    /* Past the uploaded data means the slot reads whatever follows it in
       sound RAM -- the failure the software mixer's `>=` end tests were
       widened to prevent. Refuse instead. */
    if (lea >= total)
    {
        return 0;
    }

    /* LSA and LEA are 16-bit registers. */
    if (lea > 0xFFFFu || lsa > 0xFFFFu)
    {
        return 0;
    }

    out->sa   = base;
    out->lsa  = (uint16_t)lsa;
    out->lea  = (uint16_t)lea;
    out->loop = loop;

    return 1;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `sh saturn/tests/run_tests.sh`
Expected: PASS — `all tests passed`

- [ ] **Step 6: Commit**

```bash
git add saturn/src/system/scsp_voice.h saturn/src/system/scsp_voice.cxx \
        saturn/tests/test_scsp_voice.cxx
git commit -m "Derive SCSP loop points from a chunk's geometry, with tests

The loop point is where this backend's last audible bug lived: Mixer::mix
restarted at loopPos without shifting it into 16.8 fixed point, so sustained
notes ground through the wrong region. Doing the same arithmetic for the
hardware, under test, rather than by ear."
```

---

### Task 4: Sample heap and upload cache

**Files:**
- Modify: `saturn/src/system/scsp_voice.h`
- Modify: `saturn/src/system/scsp_voice.cxx`
- Modify: `saturn/tests/test_scsp_voice.cxx`

**Interfaces:**
- Consumes: `scsp_voice.h` from Task 3.
- Produces:
  - `struct ScspCache` with `SCSP_CACHE_ENTRIES` = 32.
  - `void scsp_cache_init(ScspCache *c, uint32_t base, uint32_t limit)`
  - `void scsp_cache_reset(ScspCache *c)`
  - `ScspCacheResult scsp_cache_acquire(ScspCache *c, const uint8_t *data, uint16_t len, uint16_t loopLen, uint32_t *offsetOut)` returning one of `SCSP_CACHE_HIT`, `SCSP_CACHE_MISS`, `SCSP_CACHE_MISS_AFTER_RESET`, `SCSP_CACHE_TOO_BIG`.
  - `uint32_t scsp_cache_used_bytes(const ScspCache *c)`
  - `uint32_t scsp_cache_used_entries(const ScspCache *c)`

- [ ] **Step 1: Write the failing test**

Append to `saturn/tests/test_scsp_voice.cxx`, before `main`:

```cpp
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
```

Add the calls to `main`:

```cpp
    test_cache_miss_then_hit();
    test_cache_same_pointer_different_geometry_is_a_miss();
    test_cache_reset_forgets_everything();
    test_cache_exhaustion_resets_and_reports_it();
    test_cache_too_big_is_refused();
    test_cache_full_table_resets();
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `sh saturn/tests/run_tests.sh`
Expected: FAIL — `'ScspCache' was not declared in this scope`

- [ ] **Step 3: Add the interface**

Append to `saturn/src/system/scsp_voice.h`, inside the `extern "C"` block:

```c
/*----------------------
 | SCSP_CACHE_ENTRIES
 | Description: How many distinct samples can be resident at once. A music
 |   module carries at most 15 instruments and the VM adds a handful of effects
 |   on top, so 32 covers a part with room to spare; overflowing it costs one
 |   reload, not correctness.
 | Author: suinevere
 ----------------------*/
#define SCSP_CACHE_ENTRIES 32

typedef struct
{
    const uint8_t *key;
    uint16_t       len;
    uint16_t       loopLen;
    uint32_t       offset;
} ScspCacheEntry;

/*----------------------
 | ScspCache
 | Description: A bump allocator over a span of sound RAM, plus a table of what
 |   is already up there.
 |
 |   Bump rather than free-list because nothing is ever released individually:
 |   samples live until the resource system recycles its memory block, at which
 |   point every one of them dies at once.
 | Author: suinevere
 ----------------------*/
typedef struct
{
    uint32_t       base;
    uint32_t       limit;
    uint32_t       next;
    uint32_t       count;
    ScspCacheEntry entries[SCSP_CACHE_ENTRIES];
} ScspCache;

/*----------------------
 | ScspCacheResult
 | Description: What acquire wants the caller to do next.
 |
 |   MISS_AFTER_RESET is separate from MISS for one reason: the heap was
 |   emptied to make room, so any slot still sounding is playing out of memory
 |   that has just been handed to this upload. The caller MUST key every slot
 |   off before it copies. Making that a distinct value rather than a flag is
 |   deliberate -- it is not possible to handle the result at all without
 |   deciding what to do about it.
 | Author: suinevere
 ----------------------*/
typedef enum
{
    SCSP_CACHE_HIT              = 0,  /* resident already; no copy needed   */
    SCSP_CACHE_MISS             = 1,  /* reserved; copy to *offsetOut       */
    SCSP_CACHE_MISS_AFTER_RESET = 2,  /* as MISS, but key all slots off first */
    SCSP_CACHE_TOO_BIG          = 3   /* never fits; skip the note          */
} ScspCacheResult;

/*----------------------
 | scsp_cache_init / scsp_cache_reset
 | Description: init sets the span and empties the table; reset empties the
 |   table and rewinds the bump pointer, keeping the span.
 | Author: suinevere
 | Params: base  -- first usable sound RAM byte offset
 |         limit -- one past the last usable byte offset
 ----------------------*/
void scsp_cache_init(ScspCache *cache, uint32_t base, uint32_t limit);
void scsp_cache_reset(ScspCache *cache);

/*----------------------
 | scsp_cache_acquire
 | Description: Finds or reserves sound RAM for a sample, keyed on the address
 |   the engine holds it at.
 |
 |   Keying on the pointer is only sound because Resource::invalidateRes and
 |   Resource::invalidateAll call sat_scsp_flush_samples: the resource system
 |   is a bump allocator and a new part reuses the same addresses for different
 |   content. Comparing len and loopLen too is a cheap backstop, not the
 |   guarantee.
 | Author: suinevere
 | Params: offsetOut -- set for HIT, MISS and MISS_AFTER_RESET; untouched
 |                      for TOO_BIG
 ----------------------*/
ScspCacheResult scsp_cache_acquire(ScspCache *cache, const uint8_t *data,
                                   uint16_t len, uint16_t loopLen,
                                   uint32_t *offsetOut);

uint32_t scsp_cache_used_bytes(const ScspCache *cache);
uint32_t scsp_cache_used_entries(const ScspCache *cache);
```

- [ ] **Step 4: Write the implementation**

Append to `saturn/src/system/scsp_voice.cxx`:

```c
void scsp_cache_init(ScspCache *cache, uint32_t base, uint32_t limit)
{
    if (cache == 0)
    {
        return;
    }

    cache->base  = base;
    cache->limit = limit;
    cache->next  = base;
    cache->count = 0;
}

void scsp_cache_reset(ScspCache *cache)
{
    if (cache == 0)
    {
        return;
    }

    cache->next  = cache->base;
    cache->count = 0;
}

ScspCacheResult scsp_cache_acquire(ScspCache *cache, const uint8_t *data,
                                   uint16_t len, uint16_t loopLen,
                                   uint32_t *offsetOut)
{
    const uint32_t need = scsp_voice_upload_bytes(len, loopLen);

    uint32_t i;
    int      didReset = 0;

    if (cache == 0 || offsetOut == 0 || need == 0)
    {
        return SCSP_CACHE_TOO_BIG;
    }

    /* Larger than the span itself: flushing would not help. */
    if (need > cache->limit - cache->base)
    {
        return SCSP_CACHE_TOO_BIG;
    }

    for (i = 0; i < cache->count; i++)
    {
        const ScspCacheEntry *e = &cache->entries[i];

        if (e->key == data && e->len == len && e->loopLen == loopLen)
        {
            *offsetOut = e->offset;
            return SCSP_CACHE_HIT;
        }
    }

    /* Out of room, either in the heap or in the table. Both are recovered the
       same way and both oblige the caller to silence the slots first. */
    if (cache->next + need > cache->limit ||
        cache->count >= SCSP_CACHE_ENTRIES)
    {
        scsp_cache_reset(cache);
        didReset = 1;
    }

    cache->entries[cache->count].key     = data;
    cache->entries[cache->count].len     = len;
    cache->entries[cache->count].loopLen = loopLen;
    cache->entries[cache->count].offset  = cache->next;
    cache->count++;

    *offsetOut  = cache->next;
    cache->next += need;

    return didReset ? SCSP_CACHE_MISS_AFTER_RESET : SCSP_CACHE_MISS;
}

uint32_t scsp_cache_used_bytes(const ScspCache *cache)
{
    return (cache == 0) ? 0 : (cache->next - cache->base);
}

uint32_t scsp_cache_used_entries(const ScspCache *cache)
{
    return (cache == 0) ? 0 : cache->count;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `sh saturn/tests/run_tests.sh`
Expected: PASS — `all tests passed`

- [ ] **Step 6: Verify the Saturn build still compiles**

Run: `cd saturn && ./compile.bat debug`
Expected: builds clean.

- [ ] **Step 7: Commit**

```bash
git add saturn/src/system/scsp_voice.h saturn/src/system/scsp_voice.cxx \
        saturn/tests/test_scsp_voice.cxx
git commit -m "Sound RAM bump heap and sample upload cache, with tests

Keyed on the address the engine holds a sample at, which is only sound
because the resource system's bump allocator gets flushed alongside it. The
exhaustion path returns its own result value rather than a flag so that a
caller cannot forget to silence the slots before their memory is reused."
```

---

### Task 5: Hardware layer and a tone that settles the FNS question

This is the bring-up task. It puts a synthesised square wave through real slots with the engine entirely out of the picture, which isolates the register map, the 68000 stand-down and sound RAM writes from every other unknown. It also runs the experiment that decides whether FNS is signed.

**Files:**
- Create: `saturn/src/system/saturn_scsp.h`
- Create: `saturn/src/system/saturn_scsp.cxx`
- Modify: `saturn/src/system/saturn_audio.cxx` (call the self-test from `sat_audio_start`)

**Interfaces:**
- Consumes: `scsp_voice.h` (Tasks 2–4), `docs/scsp-registers.md` (Task 1).
- Produces:
  - `void sat_scsp_init(void)`
  - `void sat_scsp_shutdown(void)`
  - `void sat_scsp_play(uint8_t channel, const uint8_t *data, uint16_t len, uint16_t loopPos, uint16_t loopLen, uint16_t freq, uint8_t volume)`
  - `void sat_scsp_stop(uint8_t channel)`
  - `void sat_scsp_set_volume(uint8_t channel, uint8_t volume)`
  - `void sat_scsp_stop_all(void)`
  - `void sat_scsp_flush_samples(void)`
  - `void sat_scsp_self_test(void)` — removed again in Task 6.
  - Debug readouts: `uint32_t sat_scsp_debug_active(void)`, `sat_scsp_debug_heap_used(void)`, `sat_scsp_debug_cache_used(void)`, `sat_scsp_debug_uploads(void)`.

- [ ] **Step 1: Write the header**

Create `saturn/src/system/saturn_scsp.h`:

```c
/*----------------------
 | saturn_scsp.h
 | Description: The C face of the SCSP hardware mixer. mixer.cxx calls it to
 |   play, stop and re-level the engine's four channels; resource.cxx calls
 |   sat_scsp_flush_samples when it recycles the memory block those samples
 |   were read out of.
 |
 |   The arithmetic lives next door in scsp_voice.h, which has no hardware in
 |   it and is unit-tested on the host. This header is the half that writes
 |   registers.
 |
 |   Design: docs/superpowers/specs/2026-07-30-scsp-hardware-mixing-design.md
 |   Registers: docs/scsp-registers.md
 | Author: suinevere
 | Dependencies: stdint.h
 ----------------------*/
#ifndef SATURN_SCSP_H
#define SATURN_SCSP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | sat_scsp_init / sat_scsp_shutdown
 | Description: Stands the SGL 68000 sound driver down, takes the four slots,
 |   and sets the fields that never change per note. Shutdown silences them.
 | Author: suinevere
 ----------------------*/
void sat_scsp_init(void);
void sat_scsp_shutdown(void);

/*----------------------
 | sat_scsp_play
 | Description: Uploads the sample if it is not already resident, programs the
 |   slot and keys it on.
 |
 |   Takes plain scalars rather than a MixerChunk so that nothing here depends
 |   on an engine header.
 | Author: suinevere
 | Params: channel -- 0..3
 |         freq    -- playback rate in Hz, as the engine computes it
 |         volume  -- 0..63
 ----------------------*/
void sat_scsp_play(uint8_t channel, const uint8_t *data, uint16_t len,
                   uint16_t loopPos, uint16_t loopLen,
                   uint16_t freq, uint8_t volume);

void sat_scsp_stop(uint8_t channel);
void sat_scsp_set_volume(uint8_t channel, uint8_t volume);
void sat_scsp_stop_all(void);

/*----------------------
 | sat_scsp_flush_samples
 | Description: Forgets every uploaded sample and rewinds the heap. Keys all
 |   four slots off first, which is not optional: a sounding slot reads
 |   straight out of the heap, so rewinding under it hands that note's memory
 |   to the next upload.
 |
 |   Called from Resource::invalidateRes and Resource::invalidateAll, because
 |   the cache is keyed on addresses those functions recycle.
 | Author: suinevere
 ----------------------*/
void sat_scsp_flush_samples(void);

/*----------------------
 | sat_scsp_self_test
 | Description: TEMPORARY bring-up aid. Plays a synthesised square wave through
 |   slot 0 with the engine out of the picture, so the register map, the 68000
 |   stand-down and sound RAM writes can be confirmed on their own. Removed
 |   once the engine feeds the slots.
 | Author: suinevere
 ----------------------*/
void sat_scsp_self_test(void);

uint32_t sat_scsp_debug_active(void);
uint32_t sat_scsp_debug_heap_used(void);
uint32_t sat_scsp_debug_cache_used(void);
uint32_t sat_scsp_debug_uploads(void);

#ifdef __cplusplus
}
#endif
#endif /* SATURN_SCSP_H */
```

- [ ] **Step 2: Write the implementation**

Create `saturn/src/system/saturn_scsp.cxx`:

```cpp
/*----------------------
 | saturn_scsp.cxx
 | Description: Programs the SCSP's slots directly, bypassing SGL entirely.
 |
 |   This exists because SGL's slPCMOn has no loop point -- the PCM struct
 |   (sl_def.h:1402) carries mode, channel, level, pan, pitch and effect sends
 |   and nothing else -- so hardware mixing was ruled out when this backend was
 |   first designed. The slot registers underneath it do have LSA, LEA and
 |   LPCTL, which is what makes MOD-style sustained instruments possible and
 |   the software mixer unnecessary.
 |
 |   One of the files that includes <srl.hpp>.
 |   Registers: docs/scsp-registers.md
 | Author: suinevere
 | Dependencies: saturn_scsp.h, scsp_voice.h, SRL
 ----------------------*/
#include <srl.hpp>
#include "saturn_scsp.h"
#include "scsp_voice.h"

/*----------------------
 | SCSP_SLOT_BASE / SCSP_COMMON / SCSP_SOUND_RAM
 | Description: The three address bases everything here is expressed against.
 |   Slot n occupies 0x20 bytes from SCSP_SLOT_BASE.
 |
 |   Only one of these is corroborated by anything in the tree: sega_snd.h:142
 |   puts MCIRE at 0x25b0042e, which fixes the common block at 0x25B00400.
 | Author: suinevere
 ----------------------*/
#define SCSP_SLOT_BASE   0x25B00000u
#define SCSP_COMMON      0x25B00400u
#define SCSP_SOUND_RAM   0x25A00000u

#define SCSP_REG(a)      (*(volatile uint16_t *)(a))
#define SCSP_SLOT(n, o)  SCSP_REG(SCSP_SLOT_BASE + ((uint32_t)(n) * 0x20u) + (o))

/*----------------------
 | SCSP_HEAP_BASE / SCSP_HEAP_LIMIT
 | Description: The span of sound RAM samples are uploaded into: 0x020000 to
 |   0x080000, 384 KB of the bank's 512.
 |
 |   The low 128 KB is left alone because it holds the SGL driver image and
 |   every area BOOTSND.MAP declares -- the highest of which ends around
 |   0x48000. Staying above all of it means this layout does not depend on the
 |   68000 actually being stood down, which keeps the fallback in the design
 |   spec cheap if it turns out it cannot be.
 | Author: suinevere
 ----------------------*/
#define SCSP_HEAP_BASE   0x020000u
#define SCSP_HEAP_LIMIT  0x080000u

#define SCSP_CHANNELS    4

/*----------------------
 | Envelope settings
 | Description: The SCSP's envelope generator is set to do nothing: instant
 |   attack, no decay, hold until key-off, fastest release.
 |
 |   That matches what the software mixer did, which was to play the sample as
 |   it is -- Another World's instruments carry their own shape and the engine
 |   changes level by writing volume, not by asking for a ramp.
 |
 |   RR is the knob to reach for if key-off clicks: 31 is the fastest release
 |   available and so the most abrupt.
 | Author: suinevere
 ----------------------*/
#define SCSP_AR   31
#define SCSP_D1R  0
#define SCSP_D2R  0
#define SCSP_DL   0
#define SCSP_RR   31

static ScspCache g_cache;
static uint32_t  g_active  = 0;   /* bitmask of slots keyed on */
static uint32_t  g_uploads = 0;

/*----------------------
 | slotKeyOff / slotKeyOn
 | Description: KYONB is a per-slot request and KYONEX is the global commit
 |   that applies every pending request at once, so both halves are needed and
 |   the commit is written through the slot being changed.
 | Author: suinevere
 ----------------------*/
static void slotKeyOff(uint8_t slot)
{
    const uint16_t w = SCSP_SLOT(slot, 0x00);

    SCSP_SLOT(slot, 0x00) = (uint16_t)((w & ~0x0800u) | 0x1000u);
    g_active &= ~(1u << slot);
}

static void slotKeyOn(uint8_t slot)
{
    const uint16_t w = SCSP_SLOT(slot, 0x00);

    SCSP_SLOT(slot, 0x00) = (uint16_t)(w | 0x0800u | 0x1000u);
    g_active |= (1u << slot);
}

/*----------------------
 | uploadToSoundRam
 | Description: Copies a sample across, 16 bits at a time.
 |
 |   Word-at-a-time because the SCSP's RAM is 16-bit; byte writes to it are at
 |   best slow and are documented as unreliable on some revisions. The source
 |   is only guaranteed byte-aligned -- MixerChunk::data is the resource block
 |   plus 8 -- so the two bytes are assembled before the store rather than read
 |   as a uint16_t.
 | Author: suinevere
 ----------------------*/
static void uploadToSoundRam(uint32_t offset, const uint8_t *src, uint32_t bytes)
{
    volatile uint16_t *dst = (volatile uint16_t *)(SCSP_SOUND_RAM + offset);
    uint32_t           i;

    for (i = 0; i + 1u < bytes; i += 2u)
    {
        *dst++ = (uint16_t)(((uint16_t)src[i] << 8) | src[i + 1u]);
    }

    if (i < bytes)
    {
        *dst = (uint16_t)((uint16_t)src[i] << 8);
    }

    g_uploads++;
}

void sat_scsp_init(void)
{
    uint8_t n;

    /* Stand the SGL 68000 driver down and take the whole sound block. Nothing
       in this port uses CDDA or slPCM, and SRL::Core does no per-frame sound
       work -- srl_core.hpp:108 initialises sound once and never touches it
       again -- so there is nothing left relying on that driver. The SCSP is
       independent hardware and keeps playing without it.

       If this turns out to silence the SCSP on real hardware, the fallback is
       to drop this call and move the four slots up out of the SGL allocator's
       way; the sample heap already sits above every area BOOTSND.MAP
       declares, so nothing else would have to change. */
    slSoundOffWait();

    scsp_cache_init(&g_cache, SCSP_HEAP_BASE, SCSP_HEAP_LIMIT);
    g_active  = 0;
    g_uploads = 0;

    /* Master volume, full. Per-note attenuation is TL. */
    SCSP_REG(SCSP_COMMON + 0x00) =
        (uint16_t)((SCSP_REG(SCSP_COMMON + 0x00) & ~0x000Fu) | 0x000Fu);

    for (n = 0; n < SCSP_CHANNELS; n++)
    {
        slotKeyOff(n);

        SCSP_SLOT(n, 0x08) = (uint16_t)(((uint16_t)SCSP_D2R << 11) |
                                        ((uint16_t)SCSP_D1R << 6)  |
                                        (uint16_t)SCSP_AR);
        SCSP_SLOT(n, 0x0A) = (uint16_t)(((uint16_t)SCSP_DL << 5) |
                                        (uint16_t)SCSP_RR);
        SCSP_SLOT(n, 0x0C) = 0x00FF;   /* TL silent until a note sets it */

        /* DISDL 7 -- full direct send -- and DIPAN 0, centred. The engine is
           mono; there is nothing to pan. */
        SCSP_SLOT(n, 0x16) = (uint16_t)(7u << 13);
    }
}

void sat_scsp_shutdown(void)
{
    sat_scsp_stop_all();
}

void sat_scsp_stop(uint8_t channel)
{
    if (channel < SCSP_CHANNELS)
    {
        slotKeyOff(channel);
    }
}

void sat_scsp_stop_all(void)
{
    uint8_t n;

    for (n = 0; n < SCSP_CHANNELS; n++)
    {
        slotKeyOff(n);
    }
}

void sat_scsp_set_volume(uint8_t channel, uint8_t volume)
{
    if (channel >= SCSP_CHANNELS)
    {
        return;
    }

    SCSP_SLOT(channel, 0x0C) =
        (uint16_t)((SCSP_SLOT(channel, 0x0C) & ~0x00FFu) |
                   scsp_voice_tl(volume));
}

void sat_scsp_flush_samples(void)
{
    /* Silence first, then rewind. A slot reads straight out of the heap, so
       the other order hands a sounding note's memory to the next upload. */
    sat_scsp_stop_all();
    scsp_cache_reset(&g_cache);
}

void sat_scsp_play(uint8_t channel, const uint8_t *data, uint16_t len,
                   uint16_t loopPos, uint16_t loopLen,
                   uint16_t freq, uint8_t volume)
{
    ScspVoicePoints points;
    ScspCacheResult result;
    uint32_t        offset = 0;

    if (channel >= SCSP_CHANNELS || data == 0)
    {
        return;
    }

    result = scsp_cache_acquire(&g_cache, data, len, loopLen, &offset);

    if (result == SCSP_CACHE_TOO_BIG)
    {
        /* Nothing sensible to play. Leave the channel as it was rather than
           keying on a slot pointed at nothing. */
        return;
    }

    if (result == SCSP_CACHE_MISS_AFTER_RESET)
    {
        /* The heap was emptied to make room, so everything currently sounding
           is reading memory that is about to be overwritten. */
        sat_scsp_stop_all();
    }

    if (result != SCSP_CACHE_HIT)
    {
        uploadToSoundRam(offset, data, scsp_voice_upload_bytes(len, loopLen));
    }

    if (!scsp_voice_points(offset, len, loopPos, loopLen, &points))
    {
        return;
    }

    /* Key off before reprogramming: the slot may still be sounding a previous
       note and the address counter only restarts on key-on. */
    slotKeyOff(channel);

    SCSP_SLOT(channel, 0x00) =
        (uint16_t)((points.loop ? (1u << 5) : 0u) |   /* LPCTL forward loop */
                   (1u << 4) |                        /* PCM8B, 8-bit signed */
                   ((points.sa >> 16) & 0x000Fu));
    SCSP_SLOT(channel, 0x02) = (uint16_t)(points.sa & 0xFFFFu);
    SCSP_SLOT(channel, 0x04) = points.lsa;
    SCSP_SLOT(channel, 0x06) = points.lea;
    SCSP_SLOT(channel, 0x10) = scsp_voice_pitch(freq);

    sat_scsp_set_volume(channel, volume);

    slotKeyOn(channel);
}

/*----------------------
 | sat_scsp_self_test
 | Description: TEMPORARY. See the header. Three tones, each held for a second:
 |
 |     1. pitch word 0x0000 -- a 64-sample square played at 44100, so 689 Hz
 |     2. pitch word 0x7800 -- OCT -1, FNS 0, so exactly one octave down
 |     3. pitch word 0x0200 -- OCT 0, FNS 0x200
 |
 |   The third is the experiment. If FNS is SIGNED, 0x200 is -512 and the tone
 |   drops an octave, matching tone 2. If it is UNSIGNED, 0x200 is +512 and the
 |   tone rises a fifth instead. Every rate scsp_voice_pitch produces below
 |   44100 is negative, so the signed reading is the one the pitch maths needs;
 |   an unsigned SCSP would mean 22050 plays at 66150.
 |
 |   Tone 3 sounding the same as tone 2 confirms it. Tone 3 sounding higher
 |   than tone 1 refutes it, and scsp_voice_pitch has to be rewritten to bias
 |   the octave down and keep FNS positive.
 | Author: suinevere
 ----------------------*/
void sat_scsp_self_test(void)
{
    static const uint16_t kPitches[3] = { 0x0000, 0x7800, 0x0200 };
    static const char    *kLabels[3]  = { "689 Hz base", "one octave down",
                                          "FNS 0x200: down = signed" };

    uint8_t  square[64];
    uint32_t i;
    uint32_t t;

    /* A square wave rather than a sine because its shape survives being
       misread: taken as 16-bit or as stereo it stops being a clean tone in an
       obvious way. Signed, because PCM8B is signed. */
    for (i = 0; i < 64u; i++)
    {
        square[i] = (uint8_t)((i < 32u) ? 100 : (uint8_t)(int8_t)-100);
    }

    uploadToSoundRam(SCSP_HEAP_BASE, square, sizeof(square));

    for (t = 0; t < 3u; t++)
    {
        slotKeyOff(0);

        SCSP_SLOT(0, 0x00) = (uint16_t)((1u << 5) |    /* forward loop     */
                                        (1u << 4) |    /* 8-bit signed     */
                                        ((SCSP_HEAP_BASE >> 16) & 0x000Fu));
        SCSP_SLOT(0, 0x02) = (uint16_t)(SCSP_HEAP_BASE & 0xFFFFu);
        SCSP_SLOT(0, 0x04) = 0;
        SCSP_SLOT(0, 0x06) = 63;
        SCSP_SLOT(0, 0x10) = kPitches[t];
        SCSP_SLOT(0, 0x0C) = 0x0000;   /* TL 0, full */

        slotKeyOn(0);

        SRL::Debug::Print(1, 20, "SCSP TEST %d/3          ", (int)(t + 1));
        SRL::Debug::Print(1, 21, "%-28s", kLabels[t]);
        SRL::Debug::Print(1, 22, "pitch = 0x%04X   ", (unsigned)kPitches[t]);

        for (i = 0; i < 60u; i++)
        {
            SRL::Core::Synchronize();
        }
    }

    slotKeyOff(0);
    scsp_cache_reset(&g_cache);
}

uint32_t sat_scsp_debug_active(void)     { return g_active; }
uint32_t sat_scsp_debug_heap_used(void)  { return scsp_cache_used_bytes(&g_cache); }
uint32_t sat_scsp_debug_cache_used(void) { return scsp_cache_used_entries(&g_cache); }

uint32_t sat_scsp_debug_uploads(void)
{
    const uint32_t n = g_uploads;

    g_uploads = 0;
    return n;
}
```

- [ ] **Step 3: Call the self-test from startup**

In `saturn/src/system/saturn_audio.cxx`, add the include near the top, after `#include "saturn_platform.h"`:

```cpp
#include "saturn_scsp.h"
```

Then at the very top of `sat_audio_start`, before the existing `if (g_running)` guard:

```cpp
    // TEMPORARY. Bring-up only: prove the slots, the sound RAM writes and the
    // 68000 stand-down on their own, with the engine out of the picture.
    // Removed in the task that wires the mixer up.
    //
    // Returning here rather than falling through matters: the 68000 is down
    // now, and PCM_Init talks to the driver that was running on it. Letting
    // LIBPCM start against a halted sound CPU is at best pointless and at
    // worst a hang, and neither tells us anything about the tones.
    sat_scsp_init();
    sat_scsp_self_test();
    g_running = true;
    return;
```

Everything below that is now unreachable for this task, which is the intent — the only thing being exercised is the three tones.

- [ ] **Step 4: Build**

Run: `cd saturn && ./compile.bat debug`
Expected: builds clean.

If `slSoundOffWait` is not declared, add `#include <sega_snd.h>` inside an `extern "C" { }` block after `<srl.hpp>`, following the pattern `saturn_audio.cxx:34` already uses for `sega_pcm.h` — SGL headers must come after SRL's because `sgl.h` defines bare lowercase macros such as `pal`.

- [ ] **Step 5: Run on Mednafen and listen**

Run: `cd saturn && ./run_with_mednafen.bat`

Three one-second tones at boot, with the on-screen label saying which is which. Record what you hear:

| Observation | Meaning | Action |
|---|---|---|
| No sound at all | Register map wrong, or `slSoundOffWait` silences the SCSP | Re-check Task 1's map. Then try commenting out `slSoundOffWait()` and rebuild — if sound appears, take the spec's documented fallback and move the slots instead |
| Tone 1 is not roughly 689 Hz | `SCSP_BASE_RATE` or the loop registers are wrong | Re-check `LSA`/`LEA` and the base rate in Task 1's map |
| Tone 3 sounds like tone 2 (an octave below tone 1) | **FNS is signed.** `scsp_voice_pitch` is correct as written | Record this in `docs/scsp-registers.md` and continue |
| Tone 3 sounds *higher* than tone 1 (a fifth up) | **FNS is unsigned.** `scsp_voice_pitch` is wrong for every rate below 44100 | Stop. Rewrite `scsp_voice_pitch` to bias the octave down one and keep FNS positive, update every expected value in `test_pitch_known_rates`, and re-run Task 2's tests before continuing |

- [ ] **Step 6: Run on real hardware and confirm the same three tones**

Both targets must agree. If Mednafen plays the tones and hardware does not, suspect `slSoundOffWait` first — that is the one step whose behaviour is least certain.

- [ ] **Step 7: Record the finding in the register document**

Update `docs/scsp-registers.md` with the FNS result and how it was established. This is the answer Task 1 could not settle from documentation alone.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/system/saturn_scsp.h saturn/src/system/saturn_scsp.cxx \
        saturn/src/system/saturn_audio.cxx docs/scsp-registers.md
git commit -m "Drive SCSP slots directly, and settle the FNS sign by ear

A square wave through slot 0 with the engine out of the picture, so the
register map, the sound RAM writes and standing the 68000 down are confirmed
before anything depends on all three at once.

The third tone is the experiment: pitch word 0x0200 drops an octave if FNS is
signed and rises a fifth if it is not. Every rate the pitch maths produces
below 44100 is negative, so this is the assumption the whole thing rests on
and it is cheaper to hear than to argue about."
```

---

### Task 6: Wire the mixer to the hardware

**Files:**
- Modify: `saturn/src/mixer.cxx`
- Modify: `saturn/src/mixer.h`
- Modify: `saturn/src/system/saturn_system.cxx`
- Modify: `saturn/src/system/saturn_audio.cxx`
- Modify: `saturn/src/system/saturn_scsp.h`
- Modify: `saturn/src/system/saturn_scsp.cxx`

**Interfaces:**
- Consumes: `sat_scsp_*` from Task 5.
- Produces: the game's own audio through the SCSP. `Mixer::mix` and `Mixer::mixCallback` no longer exist.

- [ ] **Step 1: Remove the self-test**

Delete `sat_scsp_self_test` from both `saturn_scsp.h` and `saturn_scsp.cxx`, and delete the `sat_scsp_self_test();` call from `sat_audio_start`. Keep the `sat_scsp_init();` call.

- [ ] **Step 2: Stop starting LIBPCM**

In `saturn/src/system/saturn_audio.cxx`, replace the body of `sat_audio_start` with:

```cpp
extern "C" void sat_audio_start(SatAudioCallback callback, void *param)
{
    (void)callback;
    (void)param;

    if (g_running)
    {
        return;
    }

    sat_scsp_init();

    g_running = true;
}
```

The LIBPCM and `MODE_BUFFERS` code is now unreachable but still present; Task 8 deletes it. Leaving it for one task keeps this commit to one idea and gives a clean bisect point if the game's audio misbehaves.

Replace the body of `sat_audio_stop` with:

```cpp
extern "C" void sat_audio_stop(void)
{
    if (!g_running)
    {
        return;
    }

    sat_scsp_shutdown();

    g_running = false;
}
```

- [ ] **Step 3: Drop the callback from startAudio**

In `saturn/src/system/saturn_system.cxx`, change `startAudio` to:

```cpp
/*----------------------
 | startAudio
 | Description: The callback argument is ignored: since the SCSP does the
 |   mixing there is nothing to pull samples from. It stays in the signature
 |   because sys.h:67 is the engine's own interface and this is the only
 |   backend that does not need it.
 | Author: suinevere
 ----------------------*/
void SaturnSystem::startAudio(AudioCallback callback, void *param) {
	(void)callback;
	(void)param;
	sat_audio_start(0, 0);
}
```

- [ ] **Step 4: Turn Mixer into a shim**

In `saturn/src/mixer.cxx`, add after `#include "sys.h"`:

```cpp
#include "saturn_scsp.h"
```

Delete `addclamp` and the `g_mixClips` definition at `mixer.cxx:24-41`. Delete `Mixer::mix` and `Mixer::mixCallback` entirely.

Replace the four control methods with:

```cpp
/*----------------------
 | playChannel
 | Description: Hands a sample to an SCSP slot.
 |
 |   The MixerChannel fields are still written even though nothing reads them
 |   for playback: Mixer::saveOrLoad serialises them and the save format must
 |   not move. chunkPos and chunkInc are inert -- the hardware keeps its own
 |   position and derives pitch from freq directly.
 | Author: suinevere
 ----------------------*/
void Mixer::playChannel(uint8_t channel, const MixerChunk *mc, uint16_t freq, uint8_t volume) {
	debug(DBG_SND, "Mixer::playChannel(%d, %d, %d)", channel, freq, volume);
	assert(channel < AUDIO_NUM_CHANNELS);

	MixerChannel *ch = &_channels[channel];
	ch->volume = volume;
	ch->chunk = *mc;
	ch->chunkPos = 0;
	ch->chunkInc = 0;
	ch->active = true;

	sat_scsp_play(channel, mc->data, mc->len, mc->loopPos, mc->loopLen,
	              freq, volume);
}

void Mixer::stopChannel(uint8_t channel) {
	debug(DBG_SND, "Mixer::stopChannel(%d)", channel);
	assert(channel < AUDIO_NUM_CHANNELS);
	_channels[channel].active = false;
	sat_scsp_stop(channel);
}

void Mixer::setChannelVolume(uint8_t channel, uint8_t volume) {
	debug(DBG_SND, "Mixer::setChannelVolume(%d, %d)", channel, volume);
	assert(channel < AUDIO_NUM_CHANNELS);
	_channels[channel].volume = volume;
	sat_scsp_set_volume(channel, volume);
}

void Mixer::stopAll() {
	debug(DBG_SND, "Mixer::stopAll()");
	for (uint8_t i = 0; i < AUDIO_NUM_CHANNELS; ++i) {
		_channels[i].active = false;
	}
	sat_scsp_stop_all();
}
```

The `MutexStack` lines go with them. Add this note above `playChannel`:

```cpp
// The mutexes are gone from these methods, and that is a consequence rather
// than an oversight. They were needed when Mixer::mix ran from the vblank
// interrupt to keep the ring fed; with the SCSP mixing there is no interrupt
// and no second context, so the System mutex no-ops are correct again. The
// field ordering in playChannel -- active last -- is left as it was: it costs
// nothing and the reason it was needed is worth keeping on the record.
```

Leave `Mixer::saveOrLoad` exactly as it is, including its `lockMutex`/`unlockMutex` calls. Change `Mixer::init` to:

```cpp
void Mixer::init() {
	memset(_channels, 0, sizeof(_channels));
	_mutex = sys->createMutex();
	sys->startAudio(0, this);
}
```

- [ ] **Step 5: Remove the dead declarations**

In `saturn/src/mixer.h`, delete these two lines from `struct Mixer`:

```cpp
	void mix(int8_t *buf, int len);

	static void mixCallback(void *param, uint8_t *buf, int len);
```

Leave `MixerChunk`, `MixerChannel` and every other member alone — the save format depends on them.

- [ ] **Step 6: Key the slots off when a save is loaded**

At the end of `Mixer::saveOrLoad` in `mixer.cxx`, before `unlockMutex`, add:

```cpp
	// Whatever the channels were doing when the state was written, the
	// hardware is not doing it now. SfxPlayer serialises its own position and
	// re-issues note-ons within a tick or two; a half-finished sound effect is
	// better dropped than resumed from a position the SCSP cannot be told.
	sat_scsp_stop_all();
```

- [ ] **Step 7: Drop the last reference to the clip counter**

Deleting `g_mixClips`'s definition in `mixer.cxx` leaves `saturn_audio.cxx` referencing a symbol that no longer exists, which is a link error rather than a compile error and so will not show up until the very end of the build. Remove both halves now:

- the `extern "C" uint32_t g_mixClips;` declaration and its comment block (`saturn_audio.cxx:317-332`)
- the `g_mixClips = 0;` assignment inside `sat_audio_vblank`'s once-a-second block

Clipping is not a failure mode any more — the SCSP sums four voices with headroom where `addclamp` saturated into 8 bits.

- [ ] **Step 8: Build**

Run: `cd saturn && ./compile.bat debug`
Expected: builds clean, and links.

- [ ] **Step 9: Run and listen**

Run: `cd saturn && ./run_with_mednafen.bat`

This is the first time the game's own audio comes out of the hardware. Check, in this order:

1. Music and effects are audible.
2. Pitch is right — the intro music should be recognisable, not an octave or a fifth out.
3. Sustained notes hold rather than cutting off after their intro.
4. Effects stop on their own instead of running on into noise.

If pitch is wrong here after Task 5's tone test passed, suspect the engine-side rate rather than the register: `freq` reaches `sat_scsp_play` as the engine computed it, `7159092 / (period * 2)` in `sfxplayer.cxx:196` or `frequenceTable[freq]` in `vm.cxx:690`.

- [ ] **Step 10: Commit**

```bash
git add saturn/src/mixer.cxx saturn/src/mixer.h \
        saturn/src/system/saturn_system.cxx saturn/src/system/saturn_audio.cxx \
        saturn/src/system/saturn_scsp.h saturn/src/system/saturn_scsp.cxx
git commit -m "Mix on the SCSP: Mixer becomes a shim over four slots

Mixer::mix and mixCallback are gone. MixerChannel keeps every field because
saveOrLoad serialises them and the save format must not move, but chunkPos
and chunkInc are inert now -- the hardware keeps its own position.

The mutexes go with mix(): they existed to guard channel state against the
vblank interrupt that fed the ring, and there is no longer an interrupt to
guard against."
```

---

### Task 7: Flush the cache when resources are recycled

**Files:**
- Modify: `saturn/src/resource.cxx`

**Interfaces:**
- Consumes: `sat_scsp_flush_samples()` from Task 5.
- Produces: correct audio across part changes.

- [ ] **Step 1: Add the include**

At the top of `saturn/src/resource.cxx`, with the other includes:

```cpp
#include "saturn_scsp.h"
```

- [ ] **Step 2: Flush in both invalidation paths**

In `Resource::invalidateRes` (`resource.cxx:225`), add before `_scriptCurPtr = _scriptBakPtr;`:

```cpp
	// The SCSP's sample cache is keyed on the addresses this function is about
	// to hand back out. Without this, a new part plays the previous part's
	// audio -- and only sometimes, only after a transition, which is a
	// miserable thing to find later.
	sat_scsp_flush_samples();
```

In `Resource::invalidateAll` (`resource.cxx:237`), add the same call before `_scriptCurPtr = _memPtrStart;`, with a shorter comment:

```cpp
	// Same reason as invalidateRes: the sample cache is keyed on addresses
	// this rewinds.
	sat_scsp_flush_samples();
```

- [ ] **Step 3: Build**

Run: `cd saturn && ./compile.bat debug`
Expected: builds clean.

- [ ] **Step 4: Test the part transition**

Run: `cd saturn && ./run_with_mednafen.bat`

Play from the intro through into the first playable section, so at least one part change happens, then listen for whether the sound effects are the right ones. The failure this guards against is subtle: audio that is present and plausible but wrong, so compare against a known-good recording of the game if you have one.

Then deliberately break it to prove the test has teeth: comment out both calls, rebuild, and confirm you can hear wrong effects after a transition. Restore them afterwards.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/resource.cxx
git commit -m "Flush the SCSP sample cache when resources are recycled

The cache is keyed on the address the engine holds a sample at, and the
resource system is a bump allocator: invalidateRes and invalidateAll rewind
it, so a new part reuses the same addresses for different content. Verified
by removing these calls and hearing the previous part's effects."
```

---

### Task 8: Delete LIBPCM and rebuild the debug overlay

**Files:**
- Modify: `saturn/src/system/saturn_audio.cxx`
- Modify: `saturn/src/system/saturn_audio.h`
- Modify: `saturn/src/system/saturn_platform.cxx`

**Interfaces:**
- Consumes: everything from Tasks 5–7.
- Produces: the finished backend. `saturn_audio.cxx` holds only the timers and `sat_audio_update`.

- [ ] **Step 1: Strip saturn_audio.cxx**

Delete from `saturn/src/system/saturn_audio.cxx`:

- the `extern "C" { #include <sega_pcm.h> }` block
- `SAMPLE_RATE`, `BUFFER_SAMPLES`, `BUFFER_COUNT`, `UNCACHED`, `RING_BYTES`, `AUDIO_TEST_MODE`, `PCM_LEVEL`, `TONE_HALF_PERIOD`, `PCM_SOUND_OFFSET`, `PCM_SOUND_SAMPLES`
- `g_pcm`, `g_buffer`, `g_callback`, `g_callbackParam`, `g_playing`, `g_monoTemp`, `g_pcmWork`, `g_pcmHandle`, `g_ring`, `g_mode`, the `AudioMode` enum, `g_pumping`
- `g_rateAccum`, `g_rateTick`, `g_rateShown`, `g_maxFree`, `g_freeShown`, and the `extern "C" uint32_t g_mixClips;` declaration
- `calcPitch`, `mixInto`, `armBuffer`, `streamDiag`, `streamStart`, `streamUpdate`, `streamStop`, `pumpOutput`
- `sat_audio_vblank`

Keep `sat_audio_sample_rate` — `sys.h` still declares `getOutputSampleRate` and the engine still calls it — and change its comment to say it no longer sets pitch:

```cpp
/*----------------------
 | sat_audio_sample_rate
 | Description: The SCSP's own rate. Nothing derives pitch from this any more:
 |   sat_scsp_play is given the note's frequency directly and scsp_voice_pitch
 |   turns it into an OCT/FNS word. It stays because sys.h:70 declares
 |   getOutputSampleRate and the engine's interface is not ours to change, and
 |   it must stay non-zero because Mixer used to divide by it.
 | Author: suinevere
 ----------------------*/
extern "C" uint32_t sat_audio_sample_rate(void)
{
    return 44100;
}
```

Keep `sat_audio_start`, `sat_audio_stop`, `sat_audio_update`, `sat_timer_add`, `sat_timer_remove`, the `TimerSlot` machinery and `g_running`. Update the file's header comment to describe what it is now: the sequencer timers and the pump that services them.

- [ ] **Step 2: Add the new debug overlay to sat_audio_update**

At the end of `sat_audio_update`, replacing the old telemetry:

```cpp
    // TEMPORARY. What matters now is whether the sample cache is working.
    // UPLOAD spikes at a part change and should fall to zero once a module's
    // instruments are warm; a number that stays high means the cache is
    // missing every note and the copies are costing real time.
    {
        static uint32_t s_tick    = 0;
        static uint32_t s_uploads = 0;

        s_uploads += sat_scsp_debug_uploads();

        if (now - s_tick >= 1000)
        {
            SRL::Debug::Print(1, 20, "SLOTS %X  CACHE %d/%d   ",
                              (unsigned)sat_scsp_debug_active(),
                              (int)sat_scsp_debug_cache_used(),
                              (int)SCSP_CACHE_ENTRIES);
            SRL::Debug::Print(1, 21, "HEAP %d  UPLOAD %d      ",
                              (int)sat_scsp_debug_heap_used(),
                              (int)s_uploads);
            s_uploads = 0;
            s_tick    = now;
        }
    }
```

`saturn_audio.cxx` already includes `<srl.hpp>`, so `SRL::Debug` is available. Add `#include "scsp_voice.h"` for `SCSP_CACHE_ENTRIES`.

- [ ] **Step 3: Remove sat_audio_vblank from the header and the vblank handler**

Delete the `sat_audio_vblank` declaration and its comment block from `saturn/src/system/saturn_audio.h`.

In `saturn/src/system/saturn_platform.cxx`, reduce `onVblank` (`:95-103`) to:

```cpp
/*----------------------
 | onVblank
 | Description: Advances the frame counter. Registered with SRL::Core::OnVblank.
 |
 |   It used to clock the PCM stream driver and refill the audio ring as well.
 |   Neither exists now: the SCSP plays from its own memory and needs nothing
 |   per frame.
 | Author: suinevere
 ----------------------*/
static void onVblank()
{
    g_frames++;
}
```

- [ ] **Step 4: Update the stale comment on the update call**

In `saturn/src/system/saturn_platform.cxx` at `:213-218`, replace the comment above `sat_audio_update()`:

```cpp
    // The sequencer tick. It is no longer an audio pump -- the SCSP plays from
    // its own memory whatever the engine is doing -- but SfxPlayer's timers
    // still advance the music from here, and from the pump points in
    // Bank::unpack and sat_cd_open that keep them running during loads.
```

- [ ] **Step 5: Build**

Run: `cd saturn && ./compile.bat debug`
Expected: builds clean, with no reference to any `PCM_*` symbol left.

Confirm the driver is really gone:

```bash
grep -rn "PCM_\|sega_pcm\|slPCMOn\|RING_BYTES" saturn/src/
```
Expected: no matches.

- [ ] **Step 6: Re-run the host tests**

Run: `sh saturn/tests/run_tests.sh`
Expected: PASS — `all tests passed`

- [ ] **Step 7: Full listening pass on Mednafen and on hardware**

Work the spec's checklist in order, on both targets:

1. Any sound at all.
2. Pitch correct across the range.
3. Sustained instrument notes loop without a seam.
4. One-shot effects stop by themselves.
5. Volume ramps in music are smooth, and the four-voice balance resembles the old software mix. The hardware does not clip where `addclamp` did, so busy passages should sound cleaner — if they are now too loud, trim `MVOL` in `sat_scsp_init` or `DISDL` in the per-slot setup.
6. Play through a part change and confirm the effects are still right.
7. Latency by ear against on-screen events, and during a CD load — the loading gap should be gone, since sustained notes continue in hardware whether or not the engine is running.

Also watch the frame rate. Removing a four-voice interpolating software mixer running at 44100 samples a second should be visible as headroom; if it is not, that is worth knowing.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/system/saturn_audio.cxx saturn/src/system/saturn_audio.h \
        saturn/src/system/saturn_platform.cxx
git commit -m "Delete LIBPCM, the ring and the vblank pump

What is left of saturn_audio.cxx is the sequencer timers and the pump that
services them. The 279 ms this backend started with was 93 ms of work-RAM
ring plus 186 ms of LIBPCM staging, and neither exists now: the SCSP plays
from its own memory and a note is audible the sample after it is keyed on.

The debug overlay changes with it. Ring underruns and mix clipping are not
failure modes any more; whether the sample cache is hitting is."
```

---

## Notes for whoever executes this

**The riskiest step is Task 5, and it is deliberately early.** Everything after it assumes the register map is right and FNS is signed. If the tone test refutes either, stop and fix the foundation rather than working around it downstream — a wrong pitch model can be made to sound roughly right by compensating in two places at once, and that is very hard to unpick later.

**`calcPitch` has never actually run in anger.** It sits on the `MODE_BUFFERS` fallback path that `saturn_audio.cxx:113` says normally never runs. The game currently sounding correct says nothing about it. This is why Task 2 tests it against computed values and Task 5 checks it against the hardware.

**The cache bug is the one that will escape testing.** A stale sample cache produces audio that is present and plausible and wrong, only after a part transition. Task 7 asks you to break it on purpose and confirm you can hear the difference; do that step rather than assuming.

**Do not add features that are written down as remedies.** The spec deliberately defers eager sample preloading at `snd_playMusic` and two-slots-per-channel ping-ponging for retrigger. Each has a named trigger condition. If you do not observe the condition, do not build the remedy.
