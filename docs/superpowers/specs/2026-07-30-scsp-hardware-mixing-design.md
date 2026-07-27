# Another World → Sega Saturn — SCSP Hardware Mixing Design Spec

**Date:** 2026-07-30
**Status:** Approved, ready to plan
**Supersedes the output path of:** `2026-07-27-another-world-audio-backend-design.md`
**Target engine:** SaturnRingLib (SRL)

## Goal

Cut audio latency from ~279 ms to ~17 ms by moving mixing from the SH-2 into the
SCSP.

## Why the current design cannot get there

The existing backend mixes in software and feeds SEGA's LIBPCM stream driver.
Its latency is two buffers in series:

| Stage | Size | At 44100 Hz |
|---|---|---|
| Work-RAM ring (`RING_BYTES`) | 4096 bytes | 93 ms |
| LIBPCM staging (`pcm_size`) | 8192 samples | 186 ms |
| **Total** | | **~279 ms** |

`sega_pcm.h:520` gives `pcm_size` in samples per channel, and
`PCM_ERR_ILL_SIZE_PCMBUF` (`sega_pcm.h:67`) fixes the minimum at `4096*2`. The
staging buffer is therefore immovable. Dropping `RING_BYTES` to its 1024-byte
floor reaches ~209 ms and that is the end of the tuning available inside LIBPCM.
Two-thirds of the latency lives in a driver we do not control.

## Why hardware mixing, which was previously rejected

The 2026-07-27 spec rejected hardware mixing because "`slPCMOn` cannot loop" —
the `PCM` struct (`sl_def.h:1402`) carries mode, channel, level, pan, pitch and
effect sends, and no loop point. MOD-style instruments sustain by looping, so
hardware mixing through SGL would cut every sustained note short.

That is true of SGL's wrapper, not of the hardware. The SCSP slot registers have
`LSA`, `LEA` and `LPCTL`. Programming the slots directly removes the objection
entirely.

Three facts make direct slot access practical here:

- Nothing in the port uses SRL's sound system besides `saturn_audio.cxx` — no
  CDDA, no `slPCM*`, no sequences.
- `SRL::Core` does no per-frame sound work. `srl_core.hpp:108` calls
  `Sound::Hardware::Initialize()` once and never touches sound again.
- `MixerChunk` (`mixer.h:24`) is `data` plus `len`, `loopPos`, `loopLen`, all
  16-bit — exactly the range of an SCSP slot's loop registers.

## Architecture

Everything above `Mixer` is untouched: `SfxPlayer`, the VM, the resource system,
and the period and frequency tables all keep working as they do. `Mixer` stops
being a mixer and becomes a shim over the hardware.

```
VM / SfxPlayer
     |  playChannel(ch, chunk, freq, volume)
     |  stopChannel / setChannelVolume / stopAll
     v
Mixer  ..................... thin shim, no DSP
     v
sat_scsp_*  (saturn_scsp.cxx)
     |  1. upload chunk to sound RAM, cached
     |  2. SA / LSA / LEA / LPCTL / PCM8B      <- loop points
     |     OCT / FNS                           <- pitch
     |     TL / DISDL / DIPAN                  <- level
     |     KYONB + KYONEX                      <- key on
     v
SCSP slots 0..3   -- interpolation, looping, 4-voice sum, all in hardware
```

### Removed

`Mixer::mix`, `Mixer::mixCallback`, `addclamp`, the work-RAM ring, all of LIBPCM
(`sega_pcm.h`, every `PCM_*` call), the +128/−128 unsigned-signed conversion, the
`MODE_BUFFERS` `slPCMOn` fallback, `sat_audio_vblank` and its vblank wiring, and
the temporary telemetry: `g_mixClips`, `g_maxFree`, `g_rateAccum`, `g_rateTick`,
`g_rateShown`, `g_freeShown`.

### Retained unchanged

The timer slots and `sat_audio_update`. The music sequencer keeps being serviced
from the main loop and from the existing pump points inside `Bank::unpack` and
`sat_cd_open`'s read loop.

This is deliberate. A note-on may have to copy a sample into sound RAM, which is
a multi-millisecond memcpy, and that must not run inside the vblank interrupt.
Servicing timers where they are serviced today keeps uploads on the main loop.

### Sound-block ownership

`slSoundOffWait()` is called after SRL finishes initialising, standing the SGL
68000 driver down. This removes any possibility of the SGL slot allocator writing
over our four slots. The SCSP is independent hardware and continues to play with
the 68000 halted.

### Latency budget after the change

| Stage | Before | After |
|---|---|---|
| Work-RAM ring | 93 ms | — |
| LIBPCM staging | 186 ms | — |
| Key-on to DAC | — | ~0.02 ms (one sample frame) |
| Sequencer tick quantisation | ~17 ms | ~17 ms (one frame, unchanged) |
| **Total** | **~279 ms** | **~17 ms** |

The residual is the main loop's frame period, not the audio path. The SH-2 also
stops mixing 44100 interpolated samples per second across four voices, which
should appear as frame-rate headroom.

## Sound RAM and the sample cache

The SCSP plays only from its own 512 KB of sound RAM, so every sample must be
copied out of the resource system's memory block before it can be keyed on.

### Layout

A bump-allocated heap at sound RAM offset `0x020000`, running to `0x080000` —
384 KB. The low 128 KB is left alone: it holds the SGL driver image and the areas
`BOOTSND.MAP` declares. Leaving it untouched means the design does not depend on
the 68000 actually being halted, which keeps the fallback in "Failure handling"
cheap.

Allocations are rounded up to an even byte count because the SCSP's RAM is
16-bit. Copies use 16- or 32-bit accesses for the same reason.

### What is uploaded

Both `playChannel` call sites — `vm.cxx:681` and `sfxplayer.cxx:188` — build the
same Amiga MOD shape:

```
data    = bufPtr + 8            // skip the 8-byte header
len     = READ_BE_UINT16(bufPtr)     * 2
loopLen = READ_BE_UINT16(bufPtr + 2) * 2
loopPos = loopLen ? len : 0
```

So the upload size is always `len + loopLen`: the one-shot part followed by the
loop part. Both counts are `uint16_t`.

### The cache

A 32-entry table of `{const uint8_t *key, uint16_t len, uint16_t loopLen,
uint32_t offset}`. `playChannel` looks up `chunk.data`; a hit keys on with no
copying, a miss bumps the heap, copies, and records the entry. Music instruments
repeat constantly, so after the first pass through a pattern every note-on is
pure register writes.

### Invalidation

`chunk.data` points into the resource system's bump allocator, and
`Resource::invalidateRes` (`resource.cxx:225`) and `Resource::invalidateAll`
(`resource.cxx:237`) reset `_scriptCurPtr`. A new part reuses the same addresses
for different content, so a pointer-keyed cache that survived a part change would
play the previous part's audio.

Both functions therefore gain one call to `sat_scsp_flush_samples()`, which keys
off all four slots, clears the table and resets the heap pointer. Comparing `len`
and `loopLen` alongside the pointer is kept as a cheap second check, but the
flush is what makes this correct; the length comparison alone would not be
sufficient.

Keying off first is not optional. A slot plays directly out of the heap, so
resetting the bump pointer while a note is sounding hands that note's memory to
the next upload and the slot plays whatever lands there. Every path that resets
the heap — invalidation and the exhaustion retry below — must silence the slots
before it does so.

`Resource::allocMemBlock` (`resource.cxx:350`) also resets the bump pointer, but
runs once at startup before any audio exists. Flushing there is harmless and may
be included for symmetry; it is not required.

These two calls are the only edits outside the audio layer and `mixer.cxx`.

### Exhaustion

If a sample does not fit in the remaining heap, flush the whole heap — keying off
all four slots, as above — and retry once, then skip the note if it still does
not fit. With 384 KB against samples of a few KB this is close to unreachable,
but one frame of re-uploading beats refusing the note or writing past the end of
the heap.

### Rejected alternative

Uploading every sample of a music module eagerly at `snd_playMusic` would put all
the copying at a natural load boundary rather than on the first note of each
instrument. It requires `sfxplayer.cxx` changes and does nothing for VM sound
effects, which arrive unannounced. Ship the lazy cache, listen, and add eager
preloading only if first-pattern hitching is audible.

## SCSP slot programming

Four slots, 0–3, one per engine channel. Slot *n*'s registers are at
`0x25B00000 + n*0x20`; the common block is at `0x25B00400`. All accesses are
16-bit.

### Set once, at startup

| Field | Value | Reason |
|---|---|---|
| `MVOL` | 15 | Master volume, full. |
| `DISDL` | 7 | Direct send to the DAC, full. Per-note attenuation is `TL`. |
| `DIPAN` | 0 | Centred. The engine is mono. |
| `AR` | 31 | Instant attack. The software mixer had no envelope either. |
| `D1R` / `D2R` / `DL` | 0 / 0 / 0 | No decay; hold at `TL` for the note's life. |
| `RR` | 31 | Fastest release — `stopChannel` means stop now. First knob if key-off clicks. |
| `SSCTL` / `SBCTL` | 0 / 0 | Read from sound RAM, no inversion. |
| `PCM8B` | 1 | 8-bit signed, which is what `Mixer::mix` already read via `*(int8_t *)`. |

### Set per note-on

```
SA    = heap offset of the uploaded sample
LSA   = loopLen ? loopPos : 0
LEA   = loopLen ? loopPos + loopLen - 1 : len - 1
LPCTL = loopLen ? forward-loop : no-loop
```

With forward loop the SCSP plays `SA..LEA` and jumps back to `LSA`, so the
one-shot intro plays once and the tail sustains — the semantics `Mixer::mix`
implemented by hand, including the loop-restart position fixed in `b7e702b`.

With loop off the slot stops at `LEA` by itself. Nothing polls for the end, and
nothing in the engine reads `MixerChannel::active` outside `mix()`, so that flag
becomes inert (see "Save states").

### Pitch

`OCT`/`FNS`, computed by the existing `calcPitch()` in `saturn_audio.cxx`, which
already converts a sample rate into the SCSP's octave/FNS word. It moves across
unchanged. `chunkInc` and the pitch role of `getOutputSampleRate()` both
disappear; `sat_audio_sample_rate()` keeps returning 44100 for any remaining
caller.

### Volume

`TL` is 8 bits of attenuation in 0.375 dB steps, 0 loudest. The engine's volume
is linear 0–63, so a 64-entry lookup table maps it:

```
TL[v] = clamp(round(-20 * log10(v / 63.0) / 0.375), 0, 255)
TL[0] = key off
```

`setChannelVolume` mid-note is a single `TL` write and takes effect immediately,
which is what `sfxplayer.cxx`'s volume-up and volume-down effects need.

### Key-on

Set `KYONB` on the slot, then write `KYONEX`, which is a global commit.
Retriggering a slot that is already sounding means key off, reprogram, key on.

If that clicks or drops notes on real hardware, the documented remedy is two
slots per engine channel, ping-ponged, with the outgoing note's release covering
the seam — 8 of 32 slots. Not built up front.

### Register map must be verified first

There is no SCSP datasheet in this repository. `sega_snd.h` contributes exactly
one address, `0x25b0042e`, which lines up with `MCIRE` at common offset `0x2E`
and so corroborates the register base and the common-block layout. The bit
positions above are from general knowledge, not from a document in the tree.

**The first implementation step is to check the slot register map against a
reference** — Mednafen's `scsp.cpp` is the most convenient, and it is already a
test target. A wrong bit position is the difference between working audio and
silence, and there is no error reporting to catch it, so it is cheap to verify
before writing code against it.

## Save states

`engine.cxx:140` and `engine.cxx:174` call `mixer.saveOrLoad`, which serializes
`active`, `volume`, `chunkPos`, `chunkInc` and the whole `MixerChunk`.

`MixerChannel` keeps all of its fields and `Mixer::saveOrLoad` keeps its entry
list byte for byte, so existing save files stay readable and the serializer
version does not move. `chunkPos` and `chunkInc` become inert: written by
`playChannel`, never read.

On load, all four slots are keyed off. `SfxPlayer` serializes its own position
and re-issues note-ons within a tick or two, and a sound effect that was
half-finished at save time is better dropped than resumed.

This is the one place where "delete the software mixer" means "stop using it"
rather than "delete the struct". The dead fields are worth it to avoid breaking
saves.

## Failure handling

In rough order of likelihood:

- **Sample larger than the heap** — skip the note. That voice stays silent;
  nothing else is affected.
- **Heap exhausted** — flush and retry once, then skip. Costs one frame of
  re-uploading.
- **`slSoundOffWait()` silences the SCSP** — do not halt the 68000, and move the
  four slots up out of SGL's allocator's way. The sample heap already sits above
  the declared areas, so this fallback needs no other change.

There is no "SCSP init failed" case. Register writes do not report errors, which
is why verifying the register map comes first.

## Verification

There is no test harness on this target. Verification is a build, a run on
Mednafen, a run on real Saturn hardware, and a listening pass.

A debug overlay replaces the retired `RATE` / `EMPTY` / `CLIP` counters with:
slots currently keyed on, cache entries in use, heap bytes used, and uploads in
the last second. The upload count is the one that shows whether the cache works —
it should spike at a part change and fall to zero.

Listening checklist, ordered by what is likeliest to break:

1. **Any sound at all.** Answers the register map and the 68000 question
   together.
2. **Pitch across the range.** A wrong `OCT`/`FNS` sign is immediately audible as
   everything an octave out.
3. **Sustained instrument notes loop without a seam.** Tests `LSA` / `LEA` /
   `LPCTL`.
4. **One-shot effects stop by themselves** and do not run into whatever follows
   them in sound RAM.
5. **Volume ramps in music are smooth**, and the four-voice balance resembles the
   software mix. The hardware mix does not clip where `addclamp` did, so busy
   passages will sound cleaner and possibly louder — `MVOL` and `DISDL` are the
   trim.
6. **Play through a part change, then check sound effects are still correct.**
   This is the cache-invalidation test, and it will not show up in a short
   session.
7. **Latency by ear against on-screen events, and during a CD load.** The loading
   gap should be gone: sustained notes now continue in hardware whether or not
   the engine is doing anything.

## Out of scope

Written down as documented remedies rather than built:

- Panning — the engine is mono.
- SCSP DSP effects.
- CDDA.
- Eager preloading of module samples at `snd_playMusic`.
- Two slots per engine channel for retrigger.

## Files touched

| File | Change |
|---|---|
| `saturn/src/system/saturn_scsp.{h,cxx}` | New. Slot programming, sample heap, upload cache. |
| `saturn/src/system/saturn_audio.{h,cxx}` | Reduced to timers and `sat_audio_update`. LIBPCM, ring and telemetry removed. `calcPitch` moves to `saturn_scsp.cxx`. |
| `saturn/src/mixer.cxx` | `mix`, `mixCallback` and `addclamp` deleted. `playChannel` / `stopChannel` / `setChannelVolume` / `stopAll` become SCSP calls. `saveOrLoad` unchanged. |
| `saturn/src/mixer.h` | `MixerChannel` and `MixerChunk` unchanged, for the save format. `Mixer::mix` and `Mixer::mixCallback` declarations removed. |
| `saturn/src/resource.cxx` | Two `sat_scsp_flush_samples()` calls, in `invalidateRes` (`:225`) and `invalidateAll` (`:237`). |
| `saturn/src/system/saturn_system.cxx` | `startAudio` no longer takes a mix callback (`:98`). |
| `saturn/src/system/saturn_platform.cxx` | `sat_audio_vblank()` call removed from the vblank handler (`:102`). |
| `saturn/src/bank.cxx`, `saturn/src/system/saturn_cdfile.cxx` | Unchanged. Their `sat_audio_update()` calls still drive the sequencer. |

## Threading, and why it gets simpler

The 2026-07-27 spec made `Mixer::mix` run from the vblank interrupt so a slow
frame could not starve the ring. That forced the ordering constraint documented
at `mixer.cxx:72` — `active` set last, so the interrupt could never see a
half-described channel.

Hardware mixing removes the interrupt entirely. Nothing touches mixer state
except the main loop, so `createMutex` / `lockMutex` are no-ops and are correct
as no-ops again, and the field-ordering constraint in `playChannel` no longer
guards anything. Leave the ordering as it is — it costs nothing and the comment
records why it was needed — but nothing new needs to be made interrupt-safe.
