#!/bin/bash
# Renders the speaker's sounds with the device's own code into preview-out/speaker/ (no hardware).
set -euo pipefail
cd "$(dirname "$0")/../.."
LIB=firmware/lib/pet
mkdir -p preview-out/speaker build
clang++ -std=c++17 -O2 -Wall -Wextra -I"$LIB" tools/voice/preview_audio.cpp "$LIB/audio.cpp" "$LIB/voice_clips.cpp" -o build/preview_audio
./build/preview_audio preview-out/speaker
