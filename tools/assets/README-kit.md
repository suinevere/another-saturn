# Another World for Sega Saturn — setup kit

<!-- Shipped as README.md inside the release zip. See tools/assets/README.md
     for the source-checkout side of these scripts. -->

The disc in this kit boots but has no game in it. Another World's original data
files are not ours to give you, so `update.bat` downloads them and folds them
into the disc on your machine.

## Setup

Unzip somewhere with ~10 MB free, then:

- **Windows** — double-click `update.bat`
- **Linux / macOS** — `bash update.bat`

It downloads the data, injects it into the disc, and leaves you with:

```
Another World (USA) - Complete/
    Another World (USA).cue     <- burn or mount this
    Another World (USA).bin
```

Re-running is a no-op once the data is in place; pass `-f` to fetch it again.

## Requirements

Windows needs nothing extra — `curl` ships with Windows 10+, and `xorriso` and
`iso2raw` are bundled in `bin/`.

Linux and macOS need `xorriso` installed (`sudo apt-get install xorriso` /
`brew install xorriso`); `iso2raw` is bundled. `curl` or `wget` plus one of
`unzip`, `bsdtar` or `python3` handle the download.

## What's in here

| Path | |
|---|---|
| `update.bat` | Does everything below, in order |
| `data.bat` | Downloads the data files into `data/` |
| `CONFIG.ME` | Download URL and paths |
| `lib/` | Injection logic (`inject.sh` for POSIX, `inject.ps1` for Windows) |
| `bin/` | Bundled `xorriso` / `iso2raw` — see `bin/README.md` for licenses |
| `Another World (USA)/` | The engine-only disc image |

`update.bat` and `data.bat` are polyglot scripts: the same file runs under
`cmd.exe` and under a POSIX shell.

## Notes

The kit only ever adds `bank01`–`bank0d` and `memlist.bin` to the disc, at the
volume root. The disc's SEGA boot header is preserved byte-for-byte through the
rebuild — the scripts abort rather than emit an image that would not boot.

Avoid paths containing `'` characters; the bundled Windows `xorriso` is a
Cygwin build and its argument handling does not survive them.
