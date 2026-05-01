#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

MODEL="${1:-models/ggml-base.en.bin}"

if [ ! -f "$MODEL" ]; then
    echo "Model not found: $MODEL"
    echo "Run './init.sh' first, or pass a model path as an argument."
    exit 1
fi

echo "==> Building..."
make -j"$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)"

echo "==> Running..."
exec ./vocal-stem-stt "$MODEL"
