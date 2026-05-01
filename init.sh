#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "==> Initialising submodules..."
git submodule update --init --recursive

echo "==> Checking for cmake..."
if ! command -v cmake &>/dev/null; then
    echo "ERROR: cmake not found. Install it (e.g. 'brew install cmake' on macOS)."
    exit 1
fi

echo "==> Checking for a C++17 compiler..."
if ! command -v c++ &>/dev/null && ! command -v g++ &>/dev/null && ! command -v clang++ &>/dev/null; then
    echo "ERROR: No C++ compiler found."
    exit 1
fi

MODEL_DIR="models"
MODEL_FILE="$MODEL_DIR/ggml-base.en.bin"

if [ ! -f "$MODEL_FILE" ]; then
    echo "==> Downloading whisper base.en model..."
    mkdir -p "$MODEL_DIR"
    SCRIPT="dependencies/whisper.cpp/models/download-ggml-model.sh"
    if [ -f "$SCRIPT" ]; then
        bash "$SCRIPT" base.en
        mv "dependencies/whisper.cpp/models/ggml-base.en.bin" "$MODEL_FILE" 2>/dev/null || true
    else
        URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin"
        echo "Fetching $URL ..."
        curl -L -o "$MODEL_FILE" "$URL"
    fi
else
    echo "==> Model already present: $MODEL_FILE"
fi

echo ""
echo "Setup complete. Run './run.sh' to build and start the app."
