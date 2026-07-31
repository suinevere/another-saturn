# Data-file injection into the Saturn disc image. Source with: . lib/inject.sh
# POSIX-ish bash; used only in the bash (Linux/macOS/git-bash) code paths.
# The PowerShell twin is lib/inject.ps1 -- keep the two in step.

# file_size <path> -> bytes (GNU or BSD stat)
file_size() {
  stat -c%s "$1" 2>/dev/null || stat -f%z "$1"
}

# platform_subdir -> win | lin | mac
platform_subdir() {
  if [ -n "${OS:-}" ]; then echo win; return; fi
  case "$(uname -s)" in
    Linux)  echo lin;;
    Darwin) echo mac;;
    *)      echo unknown;;
  esac
}

# resolve_tool <name> -> executable path.
#  - iso2raw: bundled for every OS (bin/<plat>[/<arch>]/iso2raw[.exe])
#  - xorriso: bundled on Windows; system xorriso on mac/linux, and if missing
#             prints an install hint to stderr and returns 1 (does NOT exit).
resolve_tool() {
  name="$1"; plat=$(platform_subdir)
  case "$name" in
    iso2raw)
      case "$plat" in
        win) echo "./bin/win/iso2raw.exe";;
        lin) echo "./bin/lin/iso2raw";;
        mac) if [ "$(uname -m)" = "arm64" ]; then echo "./bin/mac/arm64/iso2raw";
             else echo "./bin/mac/amd64/iso2raw"; fi;;
        *)   echo "ERROR: unsupported platform for iso2raw" >&2; return 1;;
      esac;;
    xorriso)
      if [ "$plat" = win ]; then echo "./bin/win/xorriso.exe"; return 0; fi
      sys=$(command -v xorriso 2>/dev/null || true)
      if [ -n "$sys" ]; then echo "$sys"; return 0; fi
      echo "ERROR: xorriso is not installed." >&2
      if [ "$plat" = mac ]; then echo "  Install it with: brew install xorriso" >&2;
      else echo "  Install it with: sudo apt-get install xorriso" >&2; fi
      return 1;;
    *) command -v "$name";;
  esac
}

# inject_data <base.iso> <data_dir> <out_dir> <disc_name>
#
# Adds the game's data files to the engine-only base ISO and re-emits the
# burnable MODE1/2352 track. The files land at the ISO ROOT, not in a subdir:
# shared.mk builds the disc with `xorrisofs ... $(ASSETS_DIR)`, so the contents
# of saturn/cd/data are the volume root, and saturn_cdfile.cxx looks names up
# flat (normalize_name drops any directory part).
inject_data() {
  base="$1"; ddir="$2"; out="$3"; name="$4"
  mkdir -p "$out"
  XORRISO=$(resolve_tool xorriso) || return 1   # prints brew/apt hint if missing
  ISO2RAW=$(resolve_tool iso2raw) || return 1
  inj="$out/${name}_injected.iso"

  # 1) hold IP.BIN (the first 16 * 2048 bytes -- the ISO system area). xorriso
  #    rewrites it on commit, and without SEGA's boot header the disc is a
  #    coaster, so we put the original bytes back afterwards.
  dd if="$base" of="$out/ip.bin" bs=2048 count=16 2>/dev/null

  # 2) map each data file to an upper-case root name. -rockridge off is
  #    REQUIRED: xorriso enables Rock Ridge by default, which adds SUSP/PX
  #    system-use fields to every directory record (34-46 bytes -> 96-132).
  #    Sega mastering never emits those, and the Saturn CD block's ISO9660
  #    parser -- the one the BIOS uses to find the first-read file 0.BIN --
  #    chokes on them, so the patched disc stops booting. -joliet off guards the
  #    same way. shared.mk's own authoring passes --norock for this reason.
  set -- -indev "$base" -outdev "$inj" -rockridge off -joliet off
  n=0
  for f in "$ddir"/bank?? "$ddir"/memlist.bin; do
    [ -f "$f" ] || continue
    b=$(basename "$f" | tr 'a-z' 'A-Z')
    set -- "$@" -map "$f" "/$b"
    n=$((n + 1))
  done
  [ "$n" -eq 14 ] || { echo "ERROR: expected 14 data files in $ddir, found $n -- run data.bat first" >&2; return 1; }

  if ! "$XORRISO" "$@" -commit >/dev/null 2>&1; then
    echo "ERROR: xorriso injection failed" >&2; return 1
  fi
  [ -s "$inj" ] && [ "$(file_size "$inj")" -gt 32768 ] || {
    echo "ERROR: xorriso produced no injected ISO (output missing or only IP.BIN-sized)" >&2; return 1; }

  # 3) restore IP.BIN onto the front, in ISO space, before raw conversion
  dd if="$out/ip.bin" of="$inj" bs=2048 count=16 conv=notrunc 2>/dev/null

  # 4) verify preservation. A temp file rather than process substitution: this
  #    has to run under plain sh as well as bash.
  head -c 32768 "$inj" > "$out/ip.check"
  cmp -s "$out/ip.bin" "$out/ip.check" || {
    echo "ERROR: IP.BIN not preserved after injection" >&2; return 1; }

  # 5) ISO -> MODE1/2352 raw, with EDC/ECC
  "$ISO2RAW" "$inj" -o "$out/${name}.bin" || { echo "ERROR: iso2raw conversion failed" >&2; return 1; }

  # 6) track-1 cue (SDK canonical form -- matches shared.mk's create_bin_cue)
  { printf 'FILE "%s.bin" BINARY\n' "$name";
    printf '  TRACK 01 MODE1/2352\n';
    printf '    INDEX 01 00:00:00\n'; } > "$out/${name}.cue"

  rm -f "$inj" "$out/ip.bin" "$out/ip.check"
  echo "Injected $n data files -> $out/${name}.bin"
}
