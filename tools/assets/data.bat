:; # === Linux & macOS execution block ===
:; # This file is a polyglot: cmd.exe reads every ':' line as a label and falls
:; # through to the Windows block below, while a POSIX shell reads ':' as the
:; # no-op builtin and runs the rest of the line. Keep the two halves in step.
:; # The file must stay LF-only (see .gitattributes) -- a CR would land inside
:; # these commands. The single-line prologues in saturn/compile.bat get away
:; # with CRLF because their trailing ';' eats the CR; a multi-line block cannot.
:; set -eu
:; cd "$(dirname "$0")"
:;
:; cfg() { sed -n "s/^$1=//p" CONFIG.ME | head -1 | tr -d '\r'; }
:; GAME_URL=$(cfg GAME_URL)
:; DATA_DIR=$(cfg DATA_DIR)
:; DATA_DIR=${DATA_DIR:-../../saturn/cd/data}
:; [ -n "$GAME_URL" ] || { echo "ERROR: GAME_URL missing from CONFIG.ME" >&2; exit 1; }
:;
:; mkdir -p "$DATA_DIR"
:; DEST=$(cd "$DATA_DIR" && pwd)
:;
:; if [ -f "$DEST/memlist.bin" ] && [ "${1:-}" != "-f" ]; then echo "Data already installed in $DEST (pass -f to refresh)."; exit 0; fi
:;
:; tmp=$(mktemp -d)
:; trap 'rm -rf "$tmp"' EXIT INT TERM
:; mkdir -p "$tmp/x"
:;
:; echo "Downloading $GAME_URL"
:; if command -v curl >/dev/null 2>&1; then curl -fL --retry 3 -o "$tmp/game.zip" "$GAME_URL";
:; elif command -v wget >/dev/null 2>&1; then wget -q -O "$tmp/game.zip" "$GAME_URL";
:; else echo "ERROR: need curl or wget on PATH" >&2; exit 1; fi
:;
:; echo "Extracting"
:; if command -v unzip >/dev/null 2>&1; then unzip -qo "$tmp/game.zip" -d "$tmp/x";
:; elif command -v bsdtar >/dev/null 2>&1; then bsdtar -xf "$tmp/game.zip" -C "$tmp/x";
:; elif command -v python3 >/dev/null 2>&1; then python3 -c 'import sys,zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$tmp/game.zip" "$tmp/x";
:; else echo "ERROR: need unzip, bsdtar or python3 on PATH" >&2; exit 1; fi
:;
:; # Install only what the engine opens: bank01..bank0d (bank.cxx builds the name
:; # with sprintf("bank%02x")) and memlist.bin (resource.cxx:74). Names go in
:; # lower-cased so .gitignore's entries match on case-sensitive filesystems;
:; # saturn_cdfile.cxx upper-cases again at lookup time for the ISO9660 names.
:; # -recurse in case a mirror wraps the archive in a folder.
:; n=0
:; for f in $(find "$tmp/x" -type f \( -iname 'bank??' -o -iname 'memlist.bin' \) | sort); do b=$(basename "$f" | tr 'A-Z' 'a-z'); cp -f "$f" "$DEST/$b"; echo "  -> $b"; n=$((n + 1)); done
:; [ "$n" -eq 14 ] || { echo "ERROR: expected 14 data files (13 banks + memlist.bin), installed $n" >&2; exit 1; }
:;
:; echo "Installed $n files into $DEST"
:; exit

@ECHO OFF
REM === Windows execution block ===
REM See the ':' prologue above for why this file is a polyglot.
SETLOCAL ENABLEEXTENSIONS
CD /D "%~dp0"

SET "GAME_URL="
SET "DATA_DIR="
FOR /F "usebackq eol=# tokens=1,* delims==" %%A IN ("CONFIG.ME") DO (
    IF "%%A"=="GAME_URL" SET "GAME_URL=%%B"
    IF "%%A"=="DATA_DIR" SET "DATA_DIR=%%B"
)
IF NOT DEFINED GAME_URL ( ECHO ERROR: GAME_URL missing from CONFIG.ME & EXIT /B 1 )
IF NOT DEFINED DATA_DIR SET "DATA_DIR=../../saturn/cd/data"

REM Normalize forward slashes so MKDIR / IF EXIST behave, then resolve to a full
REM path -- the PowerShell steps below run with their own idea of the cwd.
SET "DATA_DIR=%DATA_DIR:/=\%"
IF NOT EXIST "%DATA_DIR%" MKDIR "%DATA_DIR%"
FOR %%I IN ("%DATA_DIR%") DO SET "DEST=%%~fI"

IF EXIST "%DEST%\memlist.bin" IF NOT "%~1"=="-f" (
    ECHO Data already installed in "%DEST%" ^(pass -f to refresh^).
    EXIT /B 0
)

SET "TMP_ZIP=%TEMP%\aw_data.zip"
SET "TMP_DIR=%TEMP%\aw_data"
IF EXIST "%TMP_DIR%" RMDIR /S /Q "%TMP_DIR%"
MKDIR "%TMP_DIR%"

ECHO Downloading %GAME_URL%
curl -fL --retry 3 -o "%TMP_ZIP%" "%GAME_URL%"
IF ERRORLEVEL 1 ( ECHO ERROR: download failed & EXIT /B 1 )

ECHO Extracting
powershell -NoProfile -Command "Expand-Archive -LiteralPath '%TMP_ZIP%' -DestinationPath '%TMP_DIR%' -Force"
IF ERRORLEVEL 1 ( ECHO ERROR: extract failed & EXIT /B 1 )

REM Same selection, lower-casing and count check as the POSIX half. Use ONLY
REM single quotes inside -Command: cmd does not honour \" as an escape, and an
REM inner double quote reopens its parser (a '>' would become a redirect).
powershell -NoProfile -Command "$n=0; Get-ChildItem -LiteralPath '%TMP_DIR%' -Recurse -File | Where-Object { $_.Name -match '^(bank[0-9a-f]{2}|memlist\.bin)$' } | ForEach-Object { $t = $_.Name.ToLower(); Copy-Item -LiteralPath $_.FullName -Destination (Join-Path '%DEST%' $t) -Force; Write-Host ('  -> ' + $t); $n++ }; if ($n -ne 14) { Write-Host ('ERROR: expected 14 data files (13 banks + memlist.bin), installed ' + $n); exit 1 }; Write-Host ('Installed ' + $n + ' files into ' + '%DEST%')"
IF ERRORLEVEL 1 ( ECHO ERROR: install failed & EXIT /B 1 )

DEL /Q "%TMP_ZIP%"
RMDIR /S /Q "%TMP_DIR%"

ENDLOCAL
EXIT /B 0
