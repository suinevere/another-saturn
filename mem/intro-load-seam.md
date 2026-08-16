---
name: intro-load-seam
description: The black gap between the opening movie and the introduction cinematic, measured stage by stage and cut from 15.0s to 6.5s — what the time was, what fixed it, and what is left.
metadata:
  type: project
---

Branch `feature/publish-program-assets`, commits `ac33bd5..108d2f7` (2026-08-16). The seam is
the black between OPENING.CPK ending and the first lit frame of GAME_PART2, reached either
on boot or through the title card's idle attract.

**15.0s to 6.5s.** Every millisecond was accounted for before anything was changed, and the
row totals summed to the independently measured span on every run.

## Where the time was

| Stage | Before | After |
|---|---|---|
| Bank prefetch (`sat_cd_open`) | 10801 | 4534 |
| Uncached disc reads | 2236 | 0 |
| Read into the resource block | 2233 | 32 |
| Bytekiller unpack, 26 resources | 1216 | 1169 |
| VM frames | 700 | 715 |
| **Total** | **14950** | **6467** |

Not `setupPart`: that loads three resources out of one bank. The introduction's own script
loads 26 across five banks, and that is the seam.

## The three defects

1. **`CACHE_WINDOW_BYTES` was 8 KB.** `LoadBytes` takes a start sector and issues a fresh
   command, so **every call costs about 120 ms whatever its size** — one revolution of the
   disc. Against 27 ms to transfer 8 KB the loop ran at 54 KB/s, a sixth of the drive.
   8 KB → 32 KB → 64 KB took bank prefetch from 10801 to 4534 ms. The remaining floor is
   about 2400 ms of pure transfer for the ~720 KB the introduction reads; no window size
   reaches it.

2. **The whole-file cache `Malloc`ed and `Free`d a ~200 KB block per bank switch,**
   fragmenting High Work RAM until the allocation failed. The failure was **silent**:
   `sat_cd_open` fell back to windowed disc reads and said nothing, which is why the cost
   showed up misattributed to `Bank::read`. One permanent buffer (`cache_storage`) fixed it.

3. The 8 KB window was justified by a PCM ring holding 46 ms. **That ring does not exist**
   — the SCSP plays from its own memory now (`saturn_platform.cxx`, `onVblank`). The pump
   between windows only keeps the sequencer's timers running, so an over-long window is a
   hitch in music timing, not an underrun. Comments corrected in `saturn_cdfile.cxx`.

## Two mistakes worth not repeating

**Do not add a minimum file size to the whole-file cache.** Tried it to keep memlist.bin
from holding 256 KB in front of `CinepakPlayer::LoadMovie`; it froze the boot.
`Resource::readEntries` parses memlist.bin a byte at a time — ~2900 reads — and each became
a GFS_Load with its own seek. **File size does not predict access pattern, and the small
files are read the most finely.** The movie's memory is taken back by
`sat_cd_cache_release()` at the top of `sat_movie_open` instead, which states when the
buffer is needed rather than guessing which files deserve it.

**The first model was wrong and cost a round trip.** Predicting ~1s from bank01's 209 KB
ignored that the introduction loads 26 resources, not 3. Measuring per stage found it;
reasoning did not.

## What is left

`FILL` is 70% of what remains and is close to its transfer floor. Going below ~6.5s means
**reading less or reading earlier**, not reading faster — which is where the original
question comes back: overlapping the load with the movie. That is now cheaper than it looked
when it was raised, because the drive runs at full speed instead of a sixth of it. Note that
prefetching a whole bank to serve one resource is a thin trade — the fifth prefetch cost
1150 ms and saved 2236 ms — so a "prefetch only on the second open" heuristic is the obvious
next lever.

`UNPACK` (1169 ms) and `FRAME` (715 ms) are real work.

**Untested:** the 64 KB window under gameplay music. Safe on this seam only because the fade
holds MVOL at zero. ~330 ms of stalled sequencer per call.

Related: [[opening-sequence-and-fades]] for what the seam sits between,
[[opening-cinepak-playback]] for the movie, [[user-runs-the-emulator]] for who builds.
