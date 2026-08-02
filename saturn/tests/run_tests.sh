#!/bin/sh
# Host unit tests. Nothing here touches hardware or SRL, which is the whole
# point: this is the logic that is cheap to get wrong and cheap to test
# off-target. Each suite is its own binary because their dependencies differ.
set -e
cd "$(dirname "$0")"

# Engine sources predate -Wextra and are compiled without -Werror; our own
# files are held to the stricter bar.
ENGINE_FLAGS="-std=c++11 -Wall -O1 -g"
OWN_FLAGS="-std=c++11 -Wall -Wextra -Werror -O1 -g"

echo "== scsp_voice =="
g++ $OWN_FLAGS -I../src/system \
    -o run_tests_scsp test_scsp_voice.cxx ../src/system/scsp_voice.cxx
./run_tests_scsp

echo "== savefmt =="
g++ $ENGINE_FLAGS -DAUTO_DETECT_PLATFORM -I../src -I../src/system \
    -o run_tests_savefmt test_savefmt.cxx \
    ../src/serializer.cxx ../src/file.cxx ../src/util.cxx
./run_tests_savefmt

echo "== backup date =="
g++ $OWN_FLAGS -I../src/system \
    -o run_tests_bupdate test_backup_date.cxx stub_saturn_backup.cxx
./run_tests_bupdate

echo "== backup stub =="
g++ $OWN_FLAGS -I../src/system \
    -o run_tests_bupstub test_backup_stub.cxx stub_saturn_backup.cxx
./run_tests_bupstub

echo "all suites passed"
