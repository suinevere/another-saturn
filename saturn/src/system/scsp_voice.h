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
 |   The model, verified in docs/scsp-registers.md, is
 |       rate = (1 + FNS/1024) * 2^OCT * 44100
 |   with FNS UNSIGNED 0..1023 and OCT signed -8..+7.
 |
 |   This is NOT a straight port of calcPitch in saturn_audio.cxx. That function
 |   reimplemented SGL's PCM_CALC_OCT (srl_sound.hpp:375) with a plain
 |   floor(log2(n)), but SGL indexes LogTable (srl_sound.hpp:339), which is
 |   floor(log2(n)) + 1. The missing octave made every rate below 44100 come out
 |   an octave low, expressed as a negative FNS -- which the unsigned field
 |   cannot represent at all. It shipped unnoticed because calcPitch only runs
 |   on the MODE_BUFFERS fallback path that never executes.
 |
 |   Worst-case error across the engine's range (874..65082 Hz) is 0.097%.
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

#ifdef __cplusplus
}
#endif
#endif /* SCSP_VOICE_H */
