# SCSP register map (verified)

This document exists because a wrong bit position in the SCSP register map
produces silence with no error to catch it. Every task in the
`scsp-hardware-mixing` plan that programs SCSP registers directly should cite
this document rather than re-deriving bit positions from memory.

The register map printed in the Task 1 brief was a starting point written
from memory and was **not** trusted as-is. Every row below has been checked
against at least one external reference; corrections relative to the brief's
draft table are called out explicitly in "Corrections vs. the brief" below.

## References consulted

1. **Sega SCSP User's Manual** (official, 1997, English translation hosted by
   the Exodus emulator project's documentation archive). This is the primary
   authority used for register semantics.
   - Table of contents: <https://www.infochunk.com/saturn/segahtml_en/hard/scsp/sakuin.htm>
   - Table of figures/tables: <https://docs.exodusemulator.com/Archives/SSDDV25/segahtml/hard/scsp/zumokuzi.htm>
   - 4.2 Sound source register overview: <https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_20.htm>
   - 4.2.1 Loop control register (KYONEX/KYONB/SBCTL/SSCTL/SA/LSA/LEA/PCM8B/LPCTL): <https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_21.htm>
   - 4.2.2 EG register (AR/EGHOLD/D1R/D2R/RR/DL/KRS/LPSLNK): <https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_22.htm>
   - 4.2.3 FM modulation control register (SOUS/MDL/MDXSL/MDYSL/STWINH): <https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_23.htm>
   - 4.2.4 Volume register (TL/SDIR): <https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_24.htm>
   - 4.2.5 PITCH register (OCT/FNS) — the critical page for the sign question: <https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_25.htm>
   - 4.2.7 MIXER register (DISDL/DIPAN/MVOL): <https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_27.htm>
   - 4.2.12 Interrupt control register (MCIRE etc.): <https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_2c.htm>
2. **Mednafen** Saturn core, `mednafen/ss/scsp.h` and `scsp.inc` (the `.inc`
   file holds the register-decode `switch` and the per-sample synthesis loop;
   there is no separate `scsp.cpp` in this tree — the brief's reference to
   `scsp.cpp` should be read as `scsp.inc`). Mirror used:
   - <https://raw.githubusercontent.com/OpenEmu/Mednafen-Core/master/mednafen/ss/scsp.h>
   - <https://raw.githubusercontent.com/OpenEmu/Mednafen-Core/master/mednafen/ss/scsp.inc>
   This is a cycle-accurate reimplementation actively used for Saturn
   emulation, and its per-sample phase-accumulator code is what settles the
   FNS sign question (see below) — it is the one source here that encodes the
   *arithmetic*, not just the bit layout.
3. **Yabause wiki**, SCSP page (community-maintained register reference,
   independent transcription effort, useful as a cross-check on bit
   positions): <https://wiki.yabause.org/index.php5?title=SCSP>
4. In-repo corroboration: `SaturnRingLib/modules/sgl/INC/sega_snd.h:142`
   defines `SND_ADR_INTR_RESET` as `0x25b0042e`. Cross-checked against all
   three sources above (see MCIRE row) — confirmed correct.

All three external sources agree on every bit position in the table below
except where noted in "Corrections vs. the brief" and "Unresolved / found in
only one source".

## Address layout

- Slot registers: `0x25B00000 + n * 0x20` for slot `n` (0–31, 32 slots — the
  SCSP is a 32-channel PCM/FM chip, confirmed by all three sources). Each
  slot occupies `0x20` bytes (16 words). All registers are 16-bit.
- Common control registers: `0x25B00400` through `0x25B0042F` (confirmed by
  the Yabause wiki's explicit address range and by Mednafen's `A < 0x430`
  bound on the common-register decode branch).
- `MCIRE` at `0x25B0042E` is corroborated by `sega_snd.h:142`
  (`SND_ADR_INTR_RESET = 0x25b0042e`) — matches: common base `0x25B00400` +
  word index `0x17` × 2 = `0x2E`. All three external sources place `MCIRE`
  at this exact address.

## Per-slot registers

Slot *n* base = `0x25B00000 + n * 0x20`. All 16-bit, big-endian on the bus.

| Offset | Bits | Field | Meaning |
|---|---|---|---|
| `+0x00` | 12 | `KYONEX` | Write 1 to commit pending `KYONB` across all 32 slots. Does not need to be cleared afterward; any slot's `KYONEX` bit works for all slots. |
| `+0x00` | 11 | `KYONB` | Key on/off request for this slot. 0 = key off, 1 = key on (registered, applied on next `KYONEX`). |
| `+0x00` | 10 | `SBCTL` bit 1 | Invert the sign bit of the source waveform data. |
| `+0x00` | 9 | `SBCTL` bit 0 | Invert the bits other than the sign bit of the source waveform data. Together bits 10–9 are commonly written as `SBCTL[1:0]`; 0 = no inversion. Default hardware behavior (SBCTL=0) reads waveform data as signed 2's-complement. |
| `+0x00` | 8–7 | `SSCTL` | Sound source. `00` = sound RAM (external DRAM) data, `01` = internally generated noise, `10` = internally generated silence (all-zero), `11` = not available/prohibited. |
| `+0x00` | 6–5 | `LPCTL` | `00` = loop off, `01` = normal (forward) loop, `10` = reverse loop, `11` = alternating loop. |
| `+0x00` | 4 | `PCM8B` | Sample format. 0 = 16-bit PCM, 2's complement. 1 = 8-bit PCM, 2's complement. **The SCSP always wants signed samples in both formats** — matches the existing repo note that the mixer's +128 bias must be removed before the SCSP sees the data. |
| `+0x00` | 3–0 | `SA[19:16]` | Start address, high nibble. |
| `+0x02` | 15–0 | `SA[15:0]` | Start address, low word. Byte address in sound RAM. If `PCM8B`=0 (16-bit data), bit 0 of the full `SA` must be written 0. |
| `+0x04` | 15–0 | `LSA` | Loop start, in samples from `SA` (bytes for 8-bit data, words for 16-bit data). |
| `+0x06` | 15–0 | `LEA` | Loop end, in samples from `SA`. |
| `+0x08` | 15–11 | `D2R` | Decay 2 rate. 0 = no change (sustain, unless `D2R`=0 explicitly means continuous sound per the manual), `1FH` = fastest. |
| `+0x08` | 10–6 | `D1R` | Decay 1 rate. `00H` = slowest (min change), `1FH` = fastest (max change). |
| `+0x08` | 5 | `EGHOLD` | Envelope hold. 1 = hold envelope at 0 (max volume) for the duration set by `AR` before entering decay; 0 = ramp per `AR` normally. |
| `+0x08` | 4–0 | `AR` | Attack rate. `00H` = slowest, `1FH` = fastest. |
| `+0x0A` | 15 | *(disputed — see note)* | Yabause wiki marks this bit unused/reserved. Mednafen's `scsp.h` decodes it as `EGBypass` ("force EG output to 0, i.e. no attenuation, but TL and ALFO still apply"). The manual page we could access (4.2.2 EG register) does not mention a bit at this position. Left unresolved here since it does not affect the FNS question and is not needed for straightforward sample playback (envelope bypass is not part of this project's plan); flagging so a later task doesn't get surprised if it reads back a nonzero value here. |
| `+0x0A` | 14 | `LPSLNK` | Loop-start link: synchronizes the start of looping with the EG's attack→decay1 transition. |
| `+0x0A` | 13–10 | `KRS` | Key rate scaling. `0H` = minimum scaling, `EH` = maximum, `FH` = scaling off. |
| `+0x0A` | 9–5 | `DL` | Decay level (upper 5 bits of the attenuation level at which Decay 1 → Decay 2 transition occurs). `00H` = max level, `1FH` = min level. |
| `+0x0A` | 4–0 | `RR` | Release rate. `00H` = slowest, `1FH` = fastest. |
| `+0x0C` | 9 | `STWINH` | Stack write inhibit — normally 0. |
| `+0x0C` | 8 | `SDIR` | Sound direct: when 1, bypasses EG/TL/ALFO (raw waveform passthrough). |
| `+0x0C` | 7–0 | `TL` | Total level (see "TL is not exactly linear" correction below). 0 = loudest (no attenuation), 255 = quietest (~‑95.7 dB). |
| `+0x0E` | 15–12 | `MDL` | Modulation level (FM sound source use only). **Missing from the brief's draft table entirely** — added here for completeness; not needed for straight PCM sample playback. |
| `+0x0E` | 11–6 | `MDXSL` | Modulation input X select (FM use only). |
| `+0x0E` | 5–0 | `MDYSL` | Modulation input Y select (FM use only). |
| `+0x10` | 15 | *(unused)* | Not part of `OCT`/`FNS` per the manual and the Yabause wiki (both mark it reserved). Mednafen's register-decode masks it into an internal `ShortWave` flag tied to short-loop handling; not corroborated by the manual pages available to us, and not relevant to basic playback. |
| `+0x10` | 14–11 | `OCT` | Octave, 4-bit **two's complement**, range −8..+7. Confirmed signed by the manual (explicit "two's complement" framing of −8 to +7), the Yabause wiki's OCT table (`0x8`=÷256 up through `0x0`=no change up to `0x7`=×128 — a two's-complement wraparound pattern), and Mednafen's `Octave ^ 0x8` sign-flip idiom used to turn it into an unsigned shift amount. |
| `+0x10` | 10 | *(unused)* | Not part of `FNS` per the manual (`FNS[9:0]`, 10 bits only) and per the Yabause wiki's bit table (explicitly marks this bit reserved, separate from the 10 FNS bits). Mednafen masks `FreqNum` with `0x7FF` (11 bits, including this one) but its phase-accumulator arithmetic only works correctly if this bit is always 0 — see the FNS section below. Treat it as reserved; do not set it. |
| `+0x10` | 9–0 | `FNS` | Frequency number switch, 10 bits. **Unsigned** — see "FNS sign resolution" below. This is the important, previously-unverified part of this document. |
| `+0x16` | 15–13 | `DISDL` | Direct (dry) send level, 0–7 (0 = mute route, per the mixer register description). |
| `+0x16` | 12–8 | `DIPAN` | Direct (dry) pan, 5 bits. 0 = centre-ish per the localization table (see manual 4.2.7 for the exact panning curve). |
| `+0x16` | 7–5 | `EFSDL` | Effect (wet) send level, 3 bits. **Not in the brief's draft table** — added for completeness; shares the `+0x16` word with `DISDL`/`DIPAN`. |
| `+0x16` | 4–0 | `EFPAN` | Effect (wet) pan, 5 bits. **Not in the brief's draft table** — added for completeness. |

Registers `+0x12` (LFO: `LFORE`/`LFOF`/`PLFOWS`/`PLFOS`/`ALFOWS`/`ALFOS`) and
`+0x14` (`ISEL`/`OMXL`, DSP input mixing) exist in the per-slot block but are
out of scope for this project (no LFO modulation or DSP routing is planned)
and are omitted from the detailed table above; they are confirmed present at
those offsets by the Yabause wiki if a later task needs them. Offsets
`+0x18`–`+0x1E` are unused (Mednafen forces writes there to 0; not documented
as carrying a field in any source consulted).

## Common block

| Address | Field | Meaning |
|---|---|---|
| `0x25B00400` bits 3–0 | `MVOL` | Master volume, 0–15 (0 = mute). Shares the word with `DAC18B` (bit 8, DAC output width) and `MEM4MB` (bit 9, sound RAM size select) — not needed for this project but noted so a stray write to this word doesn't silently touch them. |
| `0x25B0042E` | `MCIRE` | Main-CPU interrupt reset (write 1 to a bit to clear the corresponding pending bit in `MCIPD`). Corroborated by `sega_snd.h:142` and independently confirmed at this exact address by the SCSP manual, Mednafen, and the Yabause wiki. |

## FNS sign resolution

> **Confirmed on hardware, 2026-07-31.** The bring-up self-test in
> `saturn_scsp.cxx` played a 64-sample square wave through slot 0 at three pitch
> words and the result was heard as **base → octave down → fifth up**:
>
> | Pitch word | OCT | FNS | Heard | Confirms |
> |---|---|---|---|---|
> | `0x0000` | 0 | 0 | ~689 Hz | Base rate, `LSA`/`LEA`, sound RAM writes, 68000 stand-down |
> | `0x7800` | −1 | 0 | one octave **down** | `OCT` is signed and its sign runs as documented |
> | `0x0200` | 0 | 512 | a fifth **up** | `FNS` is unsigned and scales **upward** |
>
> Tone 3 rising rather than falling is the decisive observation: under a signed
> reading `0x0200` would be −512 and the tone would have dropped an octave,
> matching tone 2. It did not. The documentary conclusion below is therefore
> confirmed by measurement, not only by citation.

**Conclusion: FNS is unsigned (0–1023).** The playback-rate model is:

```
playback_rate = (1 + FNS / 1024) * 2^OCT * 44100
```

with `FNS` always non-negative and `OCT` a signed (two's complement) octave
shift that is decremented as pitch drops below the octave's top. This is the
"unsigned-FNS model" described in the Task 1 brief, and it means the current
`calcPitch()` in `saturn/src/system/saturn_audio.cxx:365` — which produces
**negative** FNS values for every rate below 44100 Hz — is producing bit
patterns that would be misinterpreted by real SCSP hardware under this model.
`calcPitch(22050)` returns the bit pattern `OCT=0, FNS=0x200`; interpreted as
unsigned (as concluded here) that plays at `(1 + 512/1024) * 1 * 44100 =
66150` Hz — a fifth sharp — instead of the intended 22050 Hz. See the brief's
Step 2 note: `calcPitch` is only used on the `MODE_BUFFERS` fallback path,
which normally never runs, so this has not caused an audible bug yet, but it
will need to be fixed (or replaced with a correct unsigned-FNS encoder)
before any task programs the SCSP pitch register directly from sample rates.

Evidence, from strongest to weakest:

1. **Mednafen's per-sample synthesis code is unambiguous and is the
   strongest evidence found.** From `scsp.inc` (line ~1615 at time of
   writing):

   ```c
   s->PhaseWhacker += (((0x400 ^ s->FreqNum) + GetPLFO(s)) << (s->Octave ^ 0x8)) >> 4;
   s->CurrentAddr += s->PhaseWhacker >> 14;
   s->PhaseWhacker &= (1U << 14) - 1;
   ```

   `s->Octave ^ 0x8` is the standard two's-complement-to-offset-binary
   sign-flip trick (turns a signed 4-bit value into an unsigned 0–15 shift
   amount) — this by itself confirms `OCT` is signed, consistent with the
   brief and the manual.

   `0x400 ^ s->FreqNum` only behaves sensibly if `FreqNum`'s bit 10 is always
   0 (which the manual and the Yabause wiki both say it is — `FNS` is a
   10-bit field, bits 9–0). Under that condition, XOR against `0x400`
   degenerates to a plain OR/add of `1024`, so the expression becomes
   `1024 + FNS`, always in the range `[1024, 2047]` — i.e. **always
   positive**, and structured exactly like an IEEE-style mantissa with an
   implicit leading 1: `(1024 + FNS) / 1024` ranges over `[1.0, 2.0)` as
   `FNS` ranges over `[0, 1023]`. That value, left-shifted by the unsigned
   octave amount and scaled, is the per-sample phase increment.

   Concretely: with `FNS=0, OCT=0`, the expression evaluates to exactly
   `1 << 14`, and `CurrentAddr` advances by exactly 1 sample per tick — i.e.
   unity playback speed, matching the manual's "if both FNS and OCT are 0H,
   the pitch will match the sampling source." There is no code path in this
   synthesis loop that would let `FreqNum` act as a two's-complement
   negative value; the arithmetic is structurally an unsigned mantissa.

2. **The SCSP manual's own worked example (Table 4.13, "FNS.OCT parameter
   table") supports the unsigned model.** For the chromatic scale from C4
   (the 44.1 kHz reference) up to C5, `OCT` stays at `0` and `FNS` climbs
   monotonically from `0` to `38D` (909 decimal) and wraps to `OCT=1, FNS=0`
   at C5. For the note **below** C4 (B3, one semitone flat), the table gives
   `OCT=F` (`0xF` = −1 in 4-bit two's complement) and `FNS=38D` (909,
   positive) — not a small negative FNS value. That is exactly the behavior
   of an unsigned fractional-octave mantissa paired with a signed octave
   exponent: going below the reference note decrements `OCT` and wraps `FNS`
   back up near its maximum, rather than letting `FNS` go negative within
   `OCT=0`.

   Checking the numbers: `(1 + 909/1024) * 2^-1 * 44100 ≈ 41623` Hz, versus
   the true B3 frequency `44100 * 2^(-1/12) ≈ 41625` Hz — a 0.005% match,
   consistent with the brief's claim of sub-0.13%-worst-case error for this
   linear-FNS approximation of the exponential pitch curve.

3. **The Yabause wiki's bit-layout table independently agrees that `FNS`
   occupies only bits 9–0** (marking bits 15 and 10 of the `+0x10` word as
   unused, separately from the 10 FNS bits and 4 OCT bits), matching the
   manual's `FNS[9:0]` notation. It does not, however, state the sign
   explicitly in prose — its OCT section explains OCT's two's-complement
   table but has no equivalent "FNS register notes" prose section (the page
   appears to be an incomplete wiki stub at that point). So this source
   corroborates the *bit width* but not directly the *sign semantics*; the
   sign conclusion rests primarily on points 1 and 2.

No source consulted supports treating `FNS` as signed. The brief noted this
was the important open question and that Task 5 would settle it empirically
with a tone test if the reference material were ambiguous — it was not
ambiguous; both an independent emulator's cycle-level synthesis arithmetic
and the official manual's own worked numeric example agree with each other
on the unsigned model. Task 5's empirical tone test is still worth running
as a hardware-truth check, but this document is no longer "unresolved, here
is the evidence" — it is a resolved conclusion with strong, mutually
corroborating evidence from two independent primary sources.

## Corrections vs. the brief's draft table

The brief's starting table was largely accurate on bit positions for the
rows it included (every row from `+0x00` through `+0x0C`, and `+0x16`,
checked bit-for-bit against Mednafen's register-decode switch and the
Yabause wiki's independent bit table, and matched exactly). The corrections
made here are:

- **`SBCTL` and `SSCTL` were under-specified.** The brief said "Sound source
  inversion. 0 = none" and "Sound source. 0 = sound RAM" without giving the
  other 1/2/3 values. Filled in from the manual and Yabause wiki: `SBCTL`
  bit 9 = invert non-sign bits, bit 10 = invert sign bit (independent
  toggles, not an enumerated mode); `SSCTL` `01`=noise, `10`=silence,
  `11`=prohibited.
- **`PCM8B` was under-specified.** The brief only documented the "1" case.
  Added the "0 = 16-bit PCM, 2's complement" case and noted both formats are
  2's complement (signed) — direct hardware-level confirmation of the
  existing repo note about needing to strip the mixer's +128 bias.
- **`TL`'s "0.375 dB per step" characterization is an approximation, not the
  literal hardware behavior.** The manual describes `TL` as a *bit-weighted*
  attenuator (bit weights −0.4, −0.8, −1.5, −3, −6, −12, −24, −48 dB for bits
  0–7), summing to −95.7 dB at `TL=0xFF` — not a perfectly linear
  0.375 dB/LSB ramp (255 × 0.375 = 95.625, close but not exact; the Yabause
  wiki independently gives "~0.3762 dB increments, ~−95.9 dB max," a third
  slightly-different rounding of the same near-linear curve). Noted in the
  table above rather than silently keeping the brief's flat "0.375 dB per
  step" framing, since a task computing attenuation from a dB target should
  know it's bit-weighted, not perfectly linear.
- **`+0x0E` (FM modulation control: `MDL`/`MDXSL`/`MDYSL`) was missing
  entirely** from the brief's per-slot table, which jumped from `+0x0C`
  straight to `+0x10`. Added, sourced from Mednafen and independently from
  the Yabause wiki (bit-for-bit agreement between the two).
- **`+0x16`'s `EFSDL`/`EFPAN` (bits 7–0 of the same word as `DISDL`/`DIPAN`)
  were missing.** Added for completeness since they share the register the
  brief already listed.
- **Bit 15 of `+0x0A` and bit 15/bit 10 of `+0x10` were silently assumed
  unused by the brief** (its bit ranges for those rows already excluded
  them, which turned out to be correct per the manual and the Yabause wiki).
  Called out explicitly in the table above rather than left implicit, since
  Mednafen decodes bit 15 of `+0x0A` as an internal `EGBypass` flag not
  corroborated elsewhere — flagged as a minor unresolved discrepancy between
  sources, not something this project's plan depends on.
- **FNS sign** — the brief flagged this as the open question to resolve; see
  the dedicated section above. Resolved to unsigned, with evidence.

Everything else in the brief's draft table (`KYONEX`=bit 12, `KYONB`=bit 11,
`LPCTL` values, `SA`/`LSA`/`LEA` widths and offsets, `D2R`/`D1R`/`EGHOLD`/`AR`
bit positions, `LPSLNK`/`KRS`/`DL`/`RR` bit positions, `STWINH`/`SDIR`/`TL`
bit positions, `OCT` bit position and signedness, `DISDL`/`DIPAN` bit
positions, the slot base formula, the common block base, and `MCIRE`'s
address) checked out exactly against all sources consulted and required no
correction.

## Not independently verified

- The exact `TL` dB curve was cross-checked against two sources (manual:
  −95.7 dB max; Yabause wiki: ~−95.9 dB max) that disagree at the second
  decimal place. Neither the manual's per-bit table nor the wiki's rounded
  figure was checked against silicon; for this project's purposes either is
  precise enough (attenuation, not pitch — no correctness-critical use).
- Bit 15 of `+0x0A` (`EGBypass` per Mednafen, unused per the Yabause wiki,
  not mentioned in the manual pages retrieved) is unresolved. Not used by
  this project's plan.
- Bit 15 of `+0x10` (`ShortWave` per Mednafen's internal naming, unused per
  the manual and the Yabause wiki) is unresolved. Not used by this project's
  plan; do not set it.
- The `DIPAN`/`EFPAN` localization curve (which of the 5 bits maps to which
  side/attenuation) was not transcribed in detail — the manual's Table 4.21
  ("Localization data by DIPAN") is the source to consult if a later task
  needs precise stereo positioning; not needed for this project's centered
  playback.
