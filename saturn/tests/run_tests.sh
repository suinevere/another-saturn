#!/bin/sh
# Host unit tests for the pure SCSP voice maths. Nothing here touches hardware
# or SRL, which is the whole point: this is the arithmetic that has historically
# been wrong in this backend and it is cheap to test off-target.
set -e
cd "$(dirname "$0")"
g++ -std=c++11 -Wall -Wextra -Werror -O1 -g \
    -I../src/system \
    -o run_tests test_scsp_voice.cxx ../src/system/scsp_voice.cxx
./run_tests
