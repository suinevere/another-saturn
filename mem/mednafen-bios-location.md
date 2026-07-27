---
name: mednafen-bios-location
description: Mednafen reads its BIOS from C:\Users\saggl\.mednafen\firmware, not the portable SaturnRingLib/emulators folder the zaturn README describes.
metadata:
  type: reference
---

On this machine Mednafen 1.32.1 reports `Base directory: C:\Users\saggl\.mednafen` and
looks for Saturn BIOS at `C:\Users\saggl\.mednafen\firmware\`. The zaturn README's
instruction to place them in `SaturnRingLib/emulators/mednafen/firmware/` does **not**
take effect — Mednafen ignores that copy.

**Why:** with the BIOS missing, Mednafen still opens a window and still prints a valid
Saturn TOC and disc header, so the disc looks like it loaded. The only symptom is a
black screen and one line at the very end of its log:

```
Error opening file "C:\Users\saggl\.mednafen\firmware\mpr-17933.bin": No such file or directory
```

**How to apply:** the BIOS is now installed in the right place (`sega_101.bin` and
`mpr-17933.bin`, 512 KB each), so this is fixed — but if a black screen ever returns,
read `SaturnRingLib/emulators/mednafen/stdout.txt` before assuming the port is at fault.
Mednafen writes nothing useful to the console on Windows; `stdout.txt` and `stderr.txt`
in its own directory are where everything goes. An empty `stderr.txt` plus an OpenGL
init block in `stdout.txt` means the emulator is genuinely running.

Related: [[user-runs-the-emulator]].
