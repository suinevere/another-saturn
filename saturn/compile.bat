:; export SRL_INSTALL_ROOT="../SaturnRingLib"; if [ "${1:-debug}" = "clean" ]; then make clean; elif [ "${1:-debug}" = "release" ]; then make all; else make all DEBUG=1; fi; exit;
@ECHO Off
REM Another-Saturn: project lives at <repo>/saturn, SDK is the sibling submodule <repo>/SaturnRingLib.
REM Stock SaturnRingLib projects sit at SaturnRingLib/Projects/<name> and reach the SDK via ../.. .
REM From <repo>/saturn we reach the SDK at ../SaturnRingLib. We put the toolchain on PATH ourselves
REM (absolute, via %~dp0) and call make directly, instead of the SDK's make.bat -- its
REM `SET COMPILER_DIR=%2` captures surrounding quotes into PATH and breaks executable lookup.
REM
REM Line 1 above is the POSIX half of this file: a shell runs it and exits, while cmd.exe reads
REM `:;` as a label and falls through to here. Keep the two halves in step.
REM
REM Usage: compile.bat [debug|release|clean]   (default: debug)
REM Output: BuildDrop/<CD_NAME>.{elf,iso,bin,cue,map} -- CD_NAME is set in the makefile.
SETLOCAL
IF "%~1"=="" (SET "TGT=debug") ELSE (SET "TGT=%~1")
SET "SRL_INSTALL_ROOT=../SaturnRingLib"
SET "CDIR=%~dp0..\SaturnRingLib\Compiler"
SET "PATH=%CDIR%\sh2eb-elf\bin;%CDIR%\msys2\usr\bin;%CDIR%\Other Utilities;%PATH%"
IF /I "%TGT%"=="clean"   GOTO doclean
IF /I "%TGT%"=="release" GOTO dorelease
make all DEBUG=1
GOTO done
:dorelease
make all
GOTO done
:doclean
make clean
GOTO done
:done
ENDLOCAL
