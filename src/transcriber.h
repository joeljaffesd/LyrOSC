#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <vector>

struct whisper_context;

// Called when a sentence/couplet boundary is detected.
// text  — the accumulated phrase
// final — true when a terminal-punctuation boundary was detected
using TranscriptCallback = std::function<void(const std::string& text, bool final)>;

class Transcriber {
public:
    Transcriber();
    ~Transcriber();

    bool load_model(const std::string& model_path);

    // Push new audio samples (mono f32, 16 kHz). Thread-safe.
    void push_audio(const std::vector<float>& samples);

    // Register callback for transcript events.
    void set_callback(TranscriptCallback cb);

    // Run one transcription iteration on the currently buffered audio.
    // Called from the processing thread in main.
    void process();

    // Returns true if the audio buffer has grown large enough for a new inference pass.
    bool ready() const;

    void reset();

private:
    whisper_context* ctx_ = nullptr;

    mutable std::mutex  buf_mutex_;
    std::vector<float>  audio_buf_;
    std::vector<float>  audio_keep_;   // tail kept across iterations

    std::string         pending_text_;  // segments awaiting sentence boundary

    TranscriptCallback  cb_;

    // Configuration (public so transcriber.cpp can reference SAMPLE_RATE as a file-scope const)
public:
    static constexpr int SAMPLE_RATE = 16000;
private:
    static constexpr int STEP_MS   = 3000;
    static constexpr int LENGTH_MS = 10000;
    static constexpr int KEEP_MS   = 200;

    bool is_sentence_end(const std::string& s) const;
    void emit(const std::string& text, bool final);
};
