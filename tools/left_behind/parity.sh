#!/bin/bash
# Exact-match check of the C++ LeftBehind (v5) against tools/left_behind/leftbehind_ref.py on whole
# SIMULATED fleet histories (10 earlier parkings + a test parking each). Default: seeds 60001-60040.
set -euo pipefail
cd "$(dirname "$0")/../.."
exec tools/.venv/bin/python tools/left_behind/parity.py "${1:-60001-60040}" "${2:-10}"
