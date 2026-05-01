# LyrOSC

Real-time speech-to-text transcription optimized for vocal stems, with output broadcast as OSC messages.

## Overview

An interactive CLI app that captures audio (live microphone or a demo WAV file), runs
Whisper-based speech recognition on it, and sends the transcribed text over UDP as OSC
messages. Natural sentence and couplet boundaries determine when a message is dispatched.

## Requirements

- CMake 3.16+
- C++17 compiler (clang++ or g++)
- macOS: Xcode command-line tools (CoreAudio is used automatically)
- Linux: ALSA or PulseAudio development headers

## Setup

After cloning, run:

```
./init.sh
```

This initialises git submodules (whisper.cpp, RTAudio) and downloads the
`ggml-base.en` Whisper model into `models/`.

## Building and running

```
./run.sh
```

`run.sh` invokes `make`, which configures and builds all dependencies via CMake,
then launches the binary. You can also do the steps separately:

```
make
./vocal-stem-stt [model-path]
```

The model path defaults to `models/ggml-base.en.bin`. Larger models (small, medium)
can be downloaded from the whisper.cpp model page and passed as the first argument.

## Startup flow

1. Select audio input: numbered list of system devices, or `0` for demo mode.
2. If a multi-channel device is chosen, select the input channel (0-based).
3. Enter the OSC destination host and port (defaults: `127.0.0.1`, `9010`).
4. Enter the OSC address pattern (default: `/transcript`).

The TUI then opens and transcription begins immediately.

## Demo mode

Option `0` plays back `media/vocals.wav` at real-time rate, feeding it through the
same transcription pipeline as live input.

## OSC output

Each completed sentence or couplet is sent as a UDP OSC packet with a single string
argument to the configured address. Partial/in-progress text is displayed in the TUI
in yellow but not transmitted until a sentence boundary is detected (`.`, `?`, `!`).

## Stopping

Press Ctrl-C once for a hint; press it again to exit cleanly.

## Dependencies

- [whisper.cpp](https://github.com/ggml-org/whisper.cpp) - local Whisper inference (submodule)
- [RTAudio](https://github.com/thestk/rtaudio) - cross-platform real-time audio I/O (submodule)
