#!/bin/bash
# Compile the real RoomAir C++ on the Mac, then check it against the Python mirror on SIMULATED rooms.
# Usage: tools/roomair/run.sh          (compile + parity check)
set -euo pipefail
cd "$(dirname "$0")/../.."
LIB=firmware/lib/pet
mkdir -p build
clang++ -std=c++17 -O2 -Wall -Wextra -I"$LIB" tools/roomair/eval.cpp "$LIB/roomair.cpp" -o build/roomair_eval
echo "built build/roomair_eval"
cd ai/room
../../tools/.venv/bin/python evaluate.py --parity
