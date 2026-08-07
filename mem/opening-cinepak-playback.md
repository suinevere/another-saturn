---
name: opening-cinepak-playback
description: Session handoff at 2dc8738 — the opening is a Cinepak movie with audio, and the two bitstream invariants ffmpeg breaks that make SEGA's decoder crash the SH-2.
metadata:
  type: project
---

Repo `C:\Users\saggl\CLionProjects\Another-Saturn`, branch `main`, **HEAD `451c3af`**.
Fifteen commits, `4ecc5f4..451c3af`, on top of the five that were already unpushed.

Supersedes [[opening-colour-pipeline]] **for the opening only**. That entry's encoder no
longer draws the opening and its open item (the navy fringe) is gone with it, but its
palette work still produces the title card through `tools/mkmenuart.py`, so do not treat
it as wholly dead. See the note in it.

## What the opening is now

`saturn/cd/data/OPENING.CPK`, built by `tools/mkopeningcpk.py` from
`images/Another World (Europe).avi` — a raw 320x224 50 fps capture of the Mega Drive boot.
The first 31.5 seconds is the front matter (SEGA, Virgin, Delphine, wordmark, lightning,
fade); everything past it is the diary text the engine already draws from bank data.

It plays through `SRL::CinepakPlayer` behind a three-call C API in
`saturn/src/system/saturn_movie.{h,cxx}`, so the rule that only the platform layer
includes `<srl.hpp>` still holds. `saturn/src/opening.cxx` keeps its old
`openingPlay`/`openingReplay` signatures, so `menu.cxx` never changed.

Video only, and that is not a preference. See the sound section below.

## The two invariants, which cost eight hardware builds

`tools/mkopeningcpk.py` ends with `verify()`, which fails the build on either of these
and names the reason. **Do not remove it.** Both were found the hard way and neither is
visible in any tool that reads the file — ffmpeg decodes the broken output cleanly and
reports nothing.

1. **Every chunk, strip, frame and sample offset must be even.** SEGA's decoder reads the
   stream with 16-bit loads and the SH-2 raises an address error on an unaligned one.
   ffmpeg does not pad; only vector chunks are ever odd, because a codebook is always
   `4 + 6n`. `align()` pads them, which is invisible to the decoder — it consumes exactly
   as many blocks as the strip geometry calls for and never reads trailing bytes. Verified
   pixel-identical afterwards (PSNR infinite against the unpadded stream).
2. **No zero-entry full codebook chunk.** `-skip_empty_cb 1`. Left at ffmpeg's default it
   emits a four-byte `0x2000`/`0x2200` inside inter strips; SEGA's decoder empties the
   codebook and the inter vectors in the same strip then index into nothing.

Also pinned, and load bearing: `-min_strips 2 -max_strips 2`. ffmpeg varies the strip
count per frame by default and the decoder does not survive it changing mid stream.
`SKYBL.CPK` is a constant 2.

`base_freq` is the one difference from SEGA's own files that could not be removed —
ffmpeg derives it from the frame rate (25) where SEGA wrote 600 with per-frame durations
of 20. It has caused no trouble. First suspect if playback timing ever looks wrong.

## Reading a Mednafen save state, which is what finally solved it

The bars-in-the-sprite instrumentation proved this was a hang rather than a stall, but the
save state is what named the bug. The technique, because it will be wanted again:

States are in `SaturnRingLib/emulators/mednafen/mcs/*.mc0`, gzip compressed, magic
`MDFNSVST`. Sections are a 32-byte null-padded name, a 4-byte LE size, then variables of
`[1-byte name length][name][4-byte LE size][data]`. `SH2-M` carries `PC`, `R` (16 words)
and `CtrlRegs` (SR, GBR, VBR). `MAIN` carries `WorkRAML` and `WorkRAMH`.

**Work RAM is stored 16-bit byte-swapped.** Undo it before reading anything, or the vector
table and every disassembly come out as garbage. Sanity check against the ELF: HWRAM
offset 0x4000 is `_PreLoader` and should start `2f 86`.

A PC around `0x0600094E`-`0x06000956` with `SR=0xF0` means the CPU took an exception and
is spinning in the BIOS handler with interrupts masked — that reads as a frozen picture,
not a crash, and the SCSP keeps looping its last buffer over a dead CPU. The faulting
address is at `[R15]` and the saved SR at `[R15+4]`. Resolve it with
`sh2eb-elf-addr2line -f -e BuildDrop/*.elf <addr>`; `nm -n` for the nearest symbol when
there is no debug info. An odd address in a register is the address-error tell.

## Five hypotheses that were wrong

Recorded because each looked convincing and each cost a build. The pattern: symptoms that
fit are not a mechanism, and a working reference beats reasoning every time.

- **The SGL sound driver.** `sat_scsp_init` stands the 68000 down and SRL documents
  CinepakPlayer as needing it. True, and irrelevant — the crash was in video decode.
- **The frame DMA into VDP1.** Disabled it; identical hang at the identical step.
- **GFS open-file limit.** Real bug, fixed, not this one — see below.
- **`slSynch` / the vblank.** Concluded from a marker that ran *before* the player's task,
  not after: `Event::Invoke` iterates static callbacks before member ones
  (`srl_event.hpp:237`), and the player's task is a member proxy.
- **Codebooks of 256 entries.** Arithmetic error on my part; the reference does it too.

The thing that actually turned it was playing SEGA's own bitstream in the port
(`SKYBL.CPK`, audio dropped with `-c:v copy` so the video stayed byte-exact). It played,
which cleared the port completely and put the fault in our file.

## Changes outside the opening

- **`SRL_MAX_CD_BACKGROUND_JOBS` 1 → 3** (`saturn/makefile`). It is the open-file limit,
  fed straight to `GFS_Init`'s `open_max`, and it sizes the GFS work area as
  `sizeof(GfsMng) + (open_max - 1) * sizeof(GfsFile)`. At 1 there is room for no
  `GfsFile` at all, and `saturn_cdfile.cxx` keeps its most recent file open on purpose,
  so the movie was a second concurrent open writing past a static array. Latent bug,
  independent of Cinepak.
- **SCSP slots moved to 24-31**, `SCSP_SLOT_FIRST` in `saturn_scsp.cxx`. Harmless with the
  68000 down; there if the driver is ever brought back up.
- **Sample heap base 0x020000 → 0x030000.** SRL puts a movie's PCM buffer at
  `0x25A20000`, which is exactly where the heap used to start. Costs 64 KB. Reclaimable
  while the openings stay silent.

## Sound

The opening carries mono 32 kHz audio, and the SGL 68000 driver is left running for it —
`slSoundOffWait` is no longer called. That means the driver and this backend share the
SCSP: the port writes slot registers and master volume directly while the driver believes
it owns the same chip. `SCSP_SLOT_FIRST` (24) keeps our voices clear of the allocator,
which hands slots out from 0 upward, and the sample heap starts above the player's PCM
buffer.

This was nearly given up on. The opening shipped silent for a while on the conclusion that
bringing the driver up hung the machine — wrong. Those builds crashed in
`cpk_VideoSampleCvid` on the alignment bug that was in every build, driver or not. The
evidence that the audio path was fine was there and misread: the symptom included one PCM
buffer looping at exactly the configured 1.02 s, which is a buffer filled once and never
refilled because the CPU had died on video.

**Fallback if the game's own music breaks:** put `slSoundOffWait()` back in
`sat_scsp_init` and re-encode the openings with `-an`. A movie with no audio track sets
`play_pcm` to 0 and is clocked off `CPK_VblIn` alone (`sgl_cpk.h:333,340`), needing neither
the driver nor sound RAM. Listen for stolen voices and dropped notes in game music before
trusting the shared arrangement.

## Environment

Build and emulator conventions are unchanged: [[srl-build-system]],
[[user-runs-the-emulator]]. **Do not run `compile.bat`** — syntax-check with
`-fsyntax-only` and the flags from `make -n src/<file>.o`. The SH-2 compiler is at
`SaturnRingLib/Compiler/sh2eb-elf/bin/`, not on PATH. ffmpeg is not on PATH either;
`mkopeningcpk.py` has the winget fallback.

`images/*.avi` and `*.mp4` are ignored — the capture is 723 MB and was staged for commit
at one point. Keep it beside the repo, not in it.

**`tools/mkopening.py` cannot run**: its source `images/genesis-opening.mp4` is gone. That
matters because `tools/mkmenuart.py` imports `load_frames` and `DARK_SUM` from it, so the
title card cannot be regenerated until the source is restored or the tool is repointed at
the AVI. Nothing is broken today — `menu_art.cxx` is committed — but the next art change
will hit this.

## Open

- Five commits from the previous session (`dcba45d..5630d2d`) plus these fifteen are still
  unpushed, and no human has read any of the diff. Code review before pushing.
- `saturn/cd/data/OPENING.CPK` is 2.3 MB at 74 KB/s, well inside the drive. The attract
  loop replay has not been watched closely; it reuses the one VDP1 texture by design.
