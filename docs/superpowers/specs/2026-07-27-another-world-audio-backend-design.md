# Another World → Sega Saturn — Audio Backend Design Spec

**Date:** 2026-07-27
**Status:** Approved, implementing
**Target engine:** SaturnRingLib (SRL)

## Goal

Give the port sound: implement the audio half of the `System` interface
(`startAudio`, `stopAudio`, `getOutputSampleRate`, `addTimer`, `removeTimer`) so
that both sound effects and music play. The video half already works.

## What the engine actually needs

Another World mixes in software. `Mixer::mix` (`mixer.cxx:95`) walks 4 voices,
linearly interpolates each, applies volume, clamps, and writes **8-bit signed
mono** samples. It handles looping itself via `MixerChunk::loopPos` / `loopLen`.

The backend is therefore a *sink*, not a synthesiser. It must:

1. Call `Mixer::mixCallback` to pull mixed samples, and get them to the speakers.
2. Fire `SfxPlayer::eventsCallback` every `_delay` ms to advance the music
   sequencer. The callback returns the next delay.
3. Report a sample rate. This is load-bearing, not cosmetic:
   `chunkInc = (freq << 8) / getOutputSampleRate()` (`mixer.cxx:62`) sets playback
   pitch, so the reported rate and the rate the hardware actually plays at must
   agree or everything is detuned.

## Why not let the SCSP do the mixing

Tempting, and nearly free: the Saturn has hardware PCM channels with hardware
resampling, and SRL exposes exactly 4 — the same number Another World uses.

Rejected, because **`slPCMOn` cannot loop**. The `PCM` struct (`sl_def.h:1402`)
carries mode, channel, level, pan, pitch and effect sends — and no loop point of
any kind. MOD-style instruments sustain by looping, so hardware mixing would cut
every sustained note short. That is a defect, not a trade-off.

The loop-capable path exists one layer down (`sega_pcm.h`: `PcmHn`,
`PCM_SetLoop`, `PCM_EntryNext`), but SRL does not wire that driver up at all.
See "Escalation path".

## Architecture

New `saturn/src/system/saturn_audio.{h,cxx}`, following the `saturn_platform`
pattern: a plain C interface, with `<srl.hpp>` confined to the `.cxx`.
`saturn_system.cxx` implements the `System` audio methods against it. `Mixer` and
`SfxPlayer` are not modified.

```
Mixer::mix (unchanged, software)
      |  8-bit signed mono
      v
sat_audio_update()   <- once per frame
      |
  [buf 0] [buf 1]    alternating, pre-mixed
      |
  slPCMOn on PCM channel 0 / 1
```

## Threading: there is none, and that matters

Everything runs from the main loop. No audio thread, no ISR callbacks.

This makes the existing `createMutex`/`lockMutex` no-ops **correct** rather than
merely harmless. The engine's comment about needing a mutex assumes SDL's audio
thread; that does not exist here. This must be documented at the implementation
so it is not later "fixed" into something that costs cycles for nothing.

The cost is that audio continuity is tied to frame pacing. CD loads happen inside
`hostFrame`, so audio will stall during long loads. Accepted for now. Moving the
refill into the vblank ISR would fix it but would make the mutexes into real
interrupt-disable critical sections, since `Mixer::playChannel` on the main loop
would then race `Mixer::mix` in the ISR.

To reduce the impact, `sat_audio_update` is called both from `sat_video_present`
and from inside `sat_sleep_ms`'s wait loop, so engine-initiated pauses keep audio
running.

## Sample rate: 11025 Hz

Replaces the placeholder 22050, which was chosen only because `mixer.cxx:62`
divides by it.

11025 halves the per-sample mixing cost, and the source material is Amiga-era
samples in that range, so a higher rate buys nothing. It also improves buffer
continuity — see below, where longer buffers mean proportionally smaller gaps.

## Output stage, and the constraint that shapes it

**`slPCMOn` will not play a buffer shorter than `0x900` bytes.** At 8-bit mono
that is 2304 samples, or 209 ms at 11025 Hz. This rules out the obvious design of
one-frame buffers swapped every vblank.

So: two buffers of 2304 samples, each bound to its own PCM channel, alternating.
Each frame, if the playing channel has gone idle, the already-mixed buffer is
started **immediately**, and only then is the just-finished buffer refilled. Order
matters — mixing first would add mixing time to the silent gap.

Consequences, stated plainly:

- **Latency is ~209 ms.** A sound effect can be up to a buffer late. Noticeable,
  and the main thing to listen for.
- **Gaps are possible.** `slPCMOn` is one-shot and "finished" can only be observed
  by polling `slPCMStat` at frame boundaries, so up to one frame (~17 ms) of
  silence can fall between buffers — about 8% of a buffer period. Whether that is
  inaudible or an obvious click depends on driver latency and cannot be
  determined by reading headers. It has to be heard.

## Cache coherency

The sound driver DMAs the buffer out of work RAM. The SH-2 writes through cache,
so mixed samples could still be sitting in cache when the DMA reads RAM, playing
stale data.

The mixer therefore writes through the **uncached mirror** of the buffer
(address `| 0x20000000`), while `slPCMOn` is handed the normal address. This is
standard Saturn practice and avoids an explicit cache flush.

## Timers

A small fixed array of slots. Only `SfxPlayer` uses this, and only one at a time,
but a 4-slot array costs nothing and avoids a single-slot special case.

Each frame, any slot whose due time has passed fires its callback; the return
value becomes the next delay, and a return of 0 retires the timer — matching the
SDL timer semantics `SfxPlayer` was written against.

## Failure behaviour

If the buffers cannot be allocated, audio disables itself and the game runs
silent. Nothing in the audio path may take down a working build. `startAudio`
failing is not fatal.

## Escalation: taken, 2026-07-27

The gaps were audible — reported as clicking and stuttering, at the ~5 Hz the
buffer period predicts. This is structural, not tunable: control only arrives at
vblank, so the boundary between buffers is always ±17 ms of gap or overlap
whatever the buffer size. Larger buffers make it rarer and laggier, smaller
buffers make it constant.

So the port now streams through SEGA's PCM driver, with the alternating-buffer
path kept as an automatic fallback.

**`LIBPCM.A` ships with the SDK but is not linked**, and nothing in
SaturnRingLib references it. `saturn/makefile` appends it to `LIBS` after the
`shared.mk` include — shared.mk assigns `LIBS` with `=`, so anything set earlier
is discarded, while the link recipe expands it lazily.

The driver is driven through `PCM_GetWriteBuf` / `PCM_NotifyWriteSize`: ask where
to write and how much room is contiguous, mix exactly that, report what was
produced. The driver reads continuously, so there is no boundary for silence to
fall into and nothing has to be timed against a frame.

### What is verified and what is not

Verified: the library links and all eleven `PCM_*` entry points resolve with
matching signatures.

**Not verified: `PCM_SOUND_OFFSET`.** `PcmCreatePara::pcm_addr` must point into
sound RAM, and no SDK sample uses this driver — `sega_pcm.h` is the only file in
the SDK that mentions it, and its comments are Shift-JIS mojibake. `BOOTSND.MAP`
(82 bytes, 8-byte records terminated by `0xFFFF`) declares only low areas, the
highest ending near `0x48000`, so the buffer is placed at `0x70000` — the top of
the 512 KB bank, which is the safest place available rather than a documented
free one.

This is the constant to suspect first if the symptom looks like driver
corruption rather than silence. A clean failure falls back and sounds exactly
like the previous build; an unclean one will not.

## Out of scope

- CD-DA music. Another World's music is sequenced from sample data in the banks,
  not redbook audio.
- Stereo. The engine mixes mono; panning would be invented, not restored.
