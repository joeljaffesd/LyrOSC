#include "audio.h"
#include "osc.h"
#include "transcriber.h"
#include "tui.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <chrono>

// ---------------------------------------------------------------------------
// Ctrl-C handling: first press prints a hint; second press quits.
// ---------------------------------------------------------------------------

static volatile int g_sigint_count = 0;

static void sigint_handler(int) {
    ++g_sigint_count;
}

// ---------------------------------------------------------------------------
// Prompt helpers
// ---------------------------------------------------------------------------

static std::string prompt(const std::string& msg, const std::string& default_val = "") {
    std::cout << msg;
    if (!default_val.empty()) std::cout << " [" << default_val << "]";
    std::cout << ": " << std::flush;
    std::string line;
    std::getline(std::cin, line);
    if (line.empty()) return default_val;
    return line;
}

static int prompt_int(const std::string& msg, int default_val) {
    std::string s = prompt(msg, std::to_string(default_val));
    try { return std::stoi(s); } catch (...) { return default_val; }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    std::signal(SIGINT, sigint_handler);

    // Model path can be overridden via first CLI arg
    std::string model_path = (argc > 1) ? argv[1]
                                        : "models/ggml-base.en.bin";

    // -----------------------------------------------------------------------
    // 1. Choose mode
    // -----------------------------------------------------------------------
    std::cout << "\n  vocal-stem-stt\n"
              << "  =============\n\n"
              << "  0) demo mode  (use media/vocals.wav)\n";

    auto devices = list_input_devices();
    for (size_t i = 0; i < devices.size(); ++i) {
        std::cout << "  " << (i + 1) << ") " << devices[i].name;
        if (devices[i].isDefaultInput) std::cout << " (default)";
        std::cout << "  [" << devices[i].inputChannels << " ch]\n";
    }

    int mode_choice = prompt_int("\nSelect (0 = demo)", 0);

    bool demo_mode = (mode_choice == 0);
    unsigned int device_id  = 0;
    unsigned int channel_idx = 0;
    std::string  device_name = "demo";

    if (!demo_mode) {
        int dev_idx = std::max(1, std::min(mode_choice, static_cast<int>(devices.size()))) - 1;
        device_id   = devices[dev_idx].id;
        device_name = devices[dev_idx].name;

        if (devices[dev_idx].inputChannels > 1) {
            channel_idx = static_cast<unsigned int>(
                prompt_int("Input channel (0-based)", 0));
            channel_idx = std::min(channel_idx, devices[dev_idx].inputChannels - 1);
        }
    }

    // -----------------------------------------------------------------------
    // 2. OSC target
    // -----------------------------------------------------------------------
    std::string osc_host = prompt("OSC host", "127.0.0.1");
    int         osc_port = prompt_int("OSC port", 9010);
    std::string osc_addr = prompt("OSC address pattern", "/transcript");

    // -----------------------------------------------------------------------
    // 3. Load model
    // -----------------------------------------------------------------------
    std::cout << "\nLoading model: " << model_path << " ...\n" << std::flush;

    Transcriber transcriber;
    if (!transcriber.load_model(model_path)) {
        std::cerr << "Failed to load whisper model: " << model_path << "\n"
                  << "Download a model with init.sh or pass path as first argument.\n";
        return 1;
    }

    // -----------------------------------------------------------------------
    // 4. Open OSC socket
    // -----------------------------------------------------------------------
    OscSender osc;
    if (!osc.open(osc_host, static_cast<uint16_t>(osc_port))) {
        std::cerr << "Failed to open UDP socket to " << osc_host << ":" << osc_port << "\n";
        return 1;
    }
    std::string osc_target = osc_host + ":" + std::to_string(osc_port);

    // -----------------------------------------------------------------------
    // 5. Set up TUI
    // -----------------------------------------------------------------------
    TuiConfig tui_cfg;
    tui_cfg.mode       = demo_mode ? "demo" : "live";
    tui_cfg.osc_target = osc_target;
    tui_cfg.device     = device_name;

    Tui tui(tui_cfg);

    std::mutex tui_mutex;

    // -----------------------------------------------------------------------
    // 6. Wire transcript callback
    // -----------------------------------------------------------------------
    transcriber.set_callback([&](const std::string& text, bool final) {
        std::lock_guard<std::mutex> lock(tui_mutex);
        tui.add_line(text, final);
        if (final) {
            osc.send(osc_addr, text);
        }
    });

    // -----------------------------------------------------------------------
    // 7. Start audio source
    // -----------------------------------------------------------------------
    std::unique_ptr<AudioSource> audio_src;

    if (demo_mode) {
        auto* demo = new DemoAudioSource();

        // Candidate paths in priority order
        std::filesystem::path exe_dir = std::filesystem::path(argv[0]).parent_path();
        std::vector<std::filesystem::path> candidates = {
            exe_dir / "media" / "vocals.wav",           // binary sits in project root
            std::filesystem::current_path() / "media" / "vocals.wav",  // cwd
            "media/vocals.wav",                          // relative fallback
        };

        std::filesystem::path wav_path;
        for (const auto& c : candidates) {
            fprintf(stderr, "[main] trying WAV path: %s -> %s\n",
                    c.string().c_str(),
                    std::filesystem::exists(c) ? "EXISTS" : "not found");
            if (std::filesystem::exists(c)) { wav_path = c; break; }
        }

        if (wav_path.empty()) {
            std::cerr << "Could not find media/vocals.wav. Tried:\n";
            for (const auto& c : candidates) std::cerr << "  " << c << "\n";
            delete demo;
            return 1;
        }

        if (!demo->load(wav_path.string())) {
            std::cerr << "Failed to load " << wav_path << "\n";
            delete demo;
            return 1;
        }
        audio_src.reset(demo);
    } else {
        audio_src = std::make_unique<LiveAudioSource>();
    }

    audio_src->start(device_id, channel_idx, [&](const std::vector<float>& samples) {
        transcriber.push_audio(samples);
    });

    // -----------------------------------------------------------------------
    // 8. Main loop
    // -----------------------------------------------------------------------
    tui.set_status("listening...");

    while (g_sigint_count < 2) {
        if (g_sigint_count == 1) {
            std::lock_guard<std::mutex> lock(tui_mutex);
            tui.set_status("Press Ctrl-C again to quit.");
        }

        if (transcriber.ready()) {
            {
                std::lock_guard<std::mutex> lock(tui_mutex);
                tui.set_status("transcribing...");
            }
            transcriber.process();
            {
                std::lock_guard<std::mutex> lock(tui_mutex);
                tui.set_status("listening...");
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // -----------------------------------------------------------------------
    // 9. Cleanup
    // -----------------------------------------------------------------------
    audio_src->stop();
    osc.close();

    // Move cursor below TUI before exit
    printf("\n  Goodbye.\n");
    return 0;
}
