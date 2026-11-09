#!/bin/bash
# Renders every mood as a GIF into preview-out/ (no hardware needed).
set -euo pipefail
cd "$(dirname "$0")/../.."
LIB=firmware/lib/pet
OUT=preview-out
mkdir -p "$OUT" build
clang++ -std=c++17 -O2 -Wall -Wextra -I"$LIB" tools/preview/preview.cpp "$LIB/pet.cpp" "$LIB/mood.cpp" -o build/preview
./build/preview "$OUT"
python3 tools/preview/frames_to_gif.py "$OUT"
echo "Done: $OUT/"
