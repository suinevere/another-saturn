# Another-Saturn: asset kit

Another World's original PC DOS data files are required to run and are not
committed. These scripts fetch them, and package a disc for people who do not
have a checkout.

`README-kit.md` is the end-user-facing copy; CI drops it into the release zip
as `README.md`. This file is the source-checkout side.

## In a checkout

```
cd tools/assets
data.bat          # Windows: double-click, or run from cmd
bash data.bat     # Linux / macOS
```

That installs `bank01`..`bank0d` and `memlist.bin` into `saturn/cd/data/`, the
CD skeleton `shared.mk` authors the ISO from. Then build as usual:

```
cd saturn && compile.bat            # or: bash compile.bat
```

Pass `-f` to re-download over an existing install; without it the script is a
no-op once `saturn/cd/data/memlist.bin` exists.

`update.bat` is for the released kit, not for a checkout — it expects a
prebuilt base ISO next to it and a local `data/` working dir. CI rewrites
`CONFIG.ME`'s `DATA_DIR` when it stages the kit.

## What gets installed

The 14 files `bank.cxx` and `resource.cxx` open. Everything else in the archive
(`another.exe`, `config.exe`, `music`, `logo`, ...) is DOS-side and has no use
on Saturn.

Names are written lower-case so the `saturn/cd/data/bank*` and
`.../memlist.bin` entries in `.gitignore` match on case-sensitive filesystems.
`normalize_name()` in `saturn/src/system/saturn_cdfile.cxx` upper-cases at
lookup time to reach the ISO9660 names on the burned disc, so on-disc case does
not matter to the engine — but `lib/inject.*` writes them upper-case anyway, to
match what `xorrisofs` produces on the normal build path.

## Two ways the data reaches a disc

| | |
|---|---|
| **Checkout / `full-image.yml`** | `data.bat` stages the files into `saturn/cd/data`, then the makefile authors the ISO with them already in it. No injection. |
| **Released kit / `update.bat`** | The disc is already built and has no data in it, so `lib/inject.sh` (or `inject.ps1`) adds the files to the existing image with `xorriso` and re-emits the MODE1/2352 track with `iso2raw`. |

Injection holds the first 16 sectors — SEGA's IP.BIN boot header — across the
rewrite and verifies them afterwards; xorriso clobbers that area on commit, and
a disc without it will not boot. Rock Ridge stays off for the same reason
`shared.mk` passes `--norock`: its SUSP/PX fields bloat the directory records
past what the Saturn CD block's ISO9660 parser tolerates.

## Configuration

`CONFIG.ME` holds `GAME_URL`, `DATA_DIR` and — for the kit — `BASE_ISO`,
`DISC_NAME` and `OUTPUT_DIR`. Relative paths resolve against `tools/assets/`,
not the caller's cwd.

## Notes

`data.bat` and `update.bat` are polyglots: `cmd.exe` reads the leading `:` lines
as labels and falls through to the Windows block, while a POSIX shell reads `:`
as the no-op builtin and runs the rest of each line. They must stay LF-only —
`.gitattributes` pins that.

The bundled Windows `xorriso` is a Cygwin build and cannot parse `C:\...`
paths; `inject.ps1` converts everything it hands over to `/cygdrive/...` form.
