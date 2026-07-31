:; # === Linux & macOS execution block ===
:; # Polyglot: cmd.exe reads every ':' line as a label and falls through to the
:; # Windows block below; a POSIX shell reads ':' as the no-op builtin and runs
:; # the rest of the line. See data.bat's header. LF-only -- see .gitattributes.
:; #
:; # One-shot setup for the released kit: fetch the game data, then fold it into
:; # the engine-only disc that ships alongside this script.
:; set -eu
:; cd "$(dirname "$0")"
:;
:; cfg() { sed -n "s/^$1=//p" CONFIG.ME | head -1 | tr -d '\r'; }
:; DATA_DIR=$(cfg DATA_DIR);     DATA_DIR=${DATA_DIR:-./data}
:; BASE_ISO=$(cfg BASE_ISO)
:; DISC_NAME=$(cfg DISC_NAME);   DISC_NAME=${DISC_NAME:-Another World (USA)}
:; OUTPUT_DIR=$(cfg OUTPUT_DIR); OUTPUT_DIR=${OUTPUT_DIR:-./$DISC_NAME - Complete}
:;
:; echo "== Step 1/2: fetch game data =="
:; sh ./data.bat "$@"
:;
:; echo "== Step 2/2: build disc =="
:; [ -f "$BASE_ISO" ] || { echo "ERROR: base ISO not found: $BASE_ISO" >&2; exit 1; }
:; . lib/inject.sh
:; inject_data "$BASE_ISO" "$DATA_DIR" "$OUTPUT_DIR" "$DISC_NAME"
:; echo
:; echo "Ready to burn or mount: $OUTPUT_DIR/$DISC_NAME.cue"
:; exit

@ECHO OFF
REM === Windows execution block ===
SETLOCAL ENABLEEXTENSIONS
CD /D "%~dp0"

SET "DATA_DIR="
SET "BASE_ISO="
SET "DISC_NAME="
SET "OUTPUT_DIR="
FOR /F "usebackq eol=# tokens=1,* delims==" %%A IN ("CONFIG.ME") DO (
    IF "%%A"=="DATA_DIR" SET "DATA_DIR=%%B"
    IF "%%A"=="BASE_ISO" SET "BASE_ISO=%%B"
    IF "%%A"=="DISC_NAME" SET "DISC_NAME=%%B"
    IF "%%A"=="OUTPUT_DIR" SET "OUTPUT_DIR=%%B"
)
IF NOT DEFINED DATA_DIR SET "DATA_DIR=./data"
IF NOT DEFINED DISC_NAME SET "DISC_NAME=Another World (USA)"
IF NOT DEFINED OUTPUT_DIR SET "OUTPUT_DIR=./%DISC_NAME% - Complete"
IF NOT DEFINED BASE_ISO SET "BASE_ISO=./%DISC_NAME%/%DISC_NAME%.iso"

REM Normalize forward slashes so IF EXIST / MKDIR behave.
SET "DATA_DIR=%DATA_DIR:/=\%"
SET "BASE_ISO=%BASE_ISO:/=\%"
SET "OUTPUT_DIR=%OUTPUT_DIR:/=\%"

ECHO == Step 1/2: fetch game data ==
CALL "%~dp0data.bat" %*
IF ERRORLEVEL 1 ( ECHO ERROR: data download failed & EXIT /B 1 )

ECHO == Step 2/2: build disc ==
REM Quote every expansion inside a parenthesized block: the disc name contains
REM ( ), which cmd would otherwise parse as block delimiters.
IF NOT EXIST "%BASE_ISO%" ( ECHO ERROR: base ISO not found: "%BASE_ISO%" & EXIT /B 1 )

powershell -NoProfile -ExecutionPolicy Bypass -File ".\lib\inject.ps1" -BaseIso "%BASE_ISO%" -DataDir "%DATA_DIR%" -OutDir "%OUTPUT_DIR%" -Name "%DISC_NAME%" -Xorriso ".\bin\win\xorriso.exe" -Iso2raw ".\bin\win\iso2raw.exe"
IF ERRORLEVEL 1 ( ECHO ERROR: disc build failed & EXIT /B 1 )

ECHO.
ECHO Ready to burn or mount: "%OUTPUT_DIR%\%DISC_NAME%.cue"

ENDLOCAL
EXIT /B 0
