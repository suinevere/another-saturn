---
name: chain-loadable-boot
description: sat_boot_init reasserts the IP hand-off state before SRL comes up, so this program boots correctly when another program loads it over itself and jumps to 0x06004000 rather than the BIOS launching it.
metadata:
  type: project
---

`sat_boot_init` calls `sat_boot_sanitize` before `SRL::Core::Initialize`
(`saturn/src/system/saturn_platform.cxx`). It exists because heart-of-the-saturn's boot menu
chain-loads this program: it reads our `0.bin` off its own disc, writes it over itself at
`0x06004000`, and jumps there. Everything below `0x06004000` survives that, so we inherit the
host's version of it.

The one that actually broke it: the BIOS user-interrupt hook table at `0x06000900 + vector * 4`
still named the host's handlers, and `slInitSystem` lifts the interrupt mask partway through its
own run and only re-hooks afterwards — so the first vblank inside that window was dispatched
into whatever our image had put at the host's addresses. `smpsys.c:156-157` clears its own two
hooks for exactly this reason before jumping to `APP_ENTRY`; we now clear `0x40`-`0x5f`.

The rest mirrors `smpsys.c`'s `msh2PeriInit` and `scuDspInit` — SH-2 DMAC, DRCR, DMAOR, DIVU,
SCU DSP — plus an SMPC `SSHOFF`. All of it is a no-op on a cold boot, where the IP has just
done the same.

Two things it deliberately does not do. **Sound**: `SRL::Sound::Hardware::Initialize` brackets
itself with `slSoundOffWait`/`slSoundOnWait`, so the M68K is reset and restarted whatever state
it arrives in, and the IP leaves it running. **The slave, properly**: by the time anything here
runs, a slave still executing the host's code has already been running through the overwrite.
Stopping it is the loader's job; doing it again here only bounds the damage.

`slSlaveOffWait` is not usable for that anywhere — it reaches `slRequestCommand`, which takes a
semaphore first and returns having done nothing when it cannot have it, and SGL reads the pads
through that same SMPC port every frame. Write `COMREG` (`0x2010001f`) directly with the `SF`
(`0x20100063`) handshake.

Related: [[user-runs-the-emulator]], [[suinevere-conventions]].
