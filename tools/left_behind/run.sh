#!/bin/bash
# Builds the harness with the real LeftBehind engine and runs it on a scenario CSV.
# Usage: tools/left_behind/run.sh scenarios.csv   (prints a CSV of detection times)
set -euo pipefail
cd "$(dirname "$0")/../.."
LIB=firmware/lib/pet
mkdir -p build
clang++ -std=c++17 -O2 -Wall -Wextra -I"$LIB" tools/left_behind/eval.cpp "$LIB/leftbehind.cpp" -o build/left_behind_eval
./build/left_behind_eval "$1"
