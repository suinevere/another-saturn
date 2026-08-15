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
 | sat_scsp_set_master
 | Description: Sets the SCSP's master volume, which is the last thing every
 |   sound on the machine passes through -- this backend's four slots, the SGL
 |   driver's voices, and the PCM the Cinepak player streams. That is what makes
 |   it the audio half of a fade: one write takes the movie and the engine down
 |   together, and neither has to know a fade is happening.
 |
 |   Read-modify-write, because MVOL shares its word with DAC18B and MEM4MB.
 | Author: suinevere
 | Params: vol -- 0 (silent) to 15 (full), clamped
 | Returns: N/A
 ----------------------*/
void sat_scsp_set_master(uint8_t vol);

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
 | sat_scsp_debug_active / sat_scsp_debug_heap_used / sat_scsp_debug_cache_used /
 | sat_scsp_debug_uploads
 | Description: Ad-hoc diagnosis, for reading back at a breakpoint or printing
 |   from a debug overlay while chasing a specific problem. Nothing calls these
 |   in normal operation.
 |
 |   sat_scsp_debug_active   -- bitmask of slots currently keyed on. A one-shot
 |     slot that has already reached its loop-end address (LEA) with looping
 |     off is silent but still reads as active here: key-on and audibility are
 |     not the same thing, and this reports the former.
 |   sat_scsp_debug_heap_used  -- bytes committed in the sample heap.
 |   sat_scsp_debug_cache_used -- number of live cache entries.
 |   sat_scsp_debug_uploads    -- upload count since the last call; reading it
 |     resets it to zero, so it is a per-interval rate, not a running total.
 | Author: suinevere
 ----------------------*/
uint32_t sat_scsp_debug_active(void);
uint32_t sat_scsp_debug_heap_used(void);
uint32_t sat_scsp_debug_cache_used(void);
uint32_t sat_scsp_debug_uploads(void);

#ifdef __cplusplus
}
#endif
#endif /* SATURN_SCSP_H */
