#include "transcriber.h"

#include <whisper.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

static constexpr int SR = Transcriber::SAMPLE_RATE;

// ---------------------------------------------------------------------------
// Simple energy-based VAD (mirrors whisper.cpp's vad_simple logic)
// ---------------------------------------------------------------------------

static bool has_voice_activity(const std::vector<float>& pcm, int last_ms, float thold) {
    int n_last = std::min(static_cast<int>(pcm.size()),
                          static_cast<int>(last_ms * SR / 1000));
    if (n_last == 0) return false;

    float energy_all = 0, energy_last = 0;
    for (float s : pcm)              energy_all  += s * s;
    for (int i = (int)pcm.size() - n_last; i < (int)pcm.size(); ++i)
        energy_last += pcm[i] * pcm[i];

    energy_all  /= pcm.size();
    energy_last /= n_last;

    return energy_last > thold * energy_all;
}

// ---------------------------------------------------------------------------
// Transcriber
// ---------------------------------------------------------------------------

Transcriber::Transcriber() = default;

Transcriber::~Transcriber() {
    if (ctx_) whisper_free(ctx_);
}

bool Transcriber::load_model(const std::string& model_path) {
    auto cparams = whisper_context_default_params();
    cparams.use_gpu    = true;
    cparams.flash_attn = true;
    ctx_ = whisper_init_from_file_with_params(model_path.c_str(), cparams);
    return ctx_ != nullptr;
}

void Transcriber::set_callback(TranscriptCallback cb) {
    cb_ = std::move(cb);
}

void Transcriber::push_audio(const std::vector<float>& samples) {
    std::lock_guard<std::mutex> lock(buf_mutex_);
    audio_buf_.insert(audio_buf_.end(), samples.begin(), samples.end());
}

bool Transcriber::ready() const {
    std::lock_guard<std::mutex> lock(buf_mutex_);
    return static_cast<int>(audio_buf_.size()) >= STEP_MS * SR / 1000;
}

void Transcriber::reset() {
    std::lock_guard<std::mutex> lock(buf_mutex_);
    audio_buf_.clear();
    audio_keep_.clear();
    pending_text_.clear();
}

bool Transcriber::is_sentence_end(const std::string& s) const {
    if (s.empty()) return false;
    // Strip trailing whitespace
    size_t i = s.size() - 1;
    while (i > 0 && (s[i] == ' ' || s[i] == '\n')) --i;
    char c = s[i];
    return c == '.' || c == '?' || c == '!';
}

void Transcriber::emit(const std::string& text, bool final) {
    if (cb_ && !text.empty()) cb_(text, final);
}

void Transcriber::process() {
    if (!ctx_) return;

    // Snapshot buffer
    std::vector<float> pcmf32;
    {
        std::lock_guard<std::mutex> lock(buf_mutex_);
        if (audio_buf_.empty()) return;

        int n_step = STEP_MS   * SR / 1000;
        int n_len  = LENGTH_MS * SR / 1000;
        int n_keep = KEEP_MS   * SR / 1000;

        int n_new  = static_cast<int>(audio_buf_.size());
        int n_take = std::min(static_cast<int>(audio_keep_.size()),
                              std::max(0, n_keep + n_len - n_new));

        pcmf32.resize(n_take + n_new);
        if (n_take > 0)
            memcpy(pcmf32.data(),
                   audio_keep_.data() + audio_keep_.size() - n_take,
                   n_take * sizeof(float));
        memcpy(pcmf32.data() + n_take, audio_buf_.data(), n_new * sizeof(float));

        // Keep tail for next iteration
        audio_keep_ = std::vector<float>(pcmf32.end() - std::min(n_keep, n_new),
                                         pcmf32.end());
        audio_buf_.clear();
    }

    // Log what we're feeding to whisper
    float peak = 0;
    for (float s : pcmf32) if (std::fabs(s) > peak) peak = std::fabs(s);
    fprintf(stderr, "[transcriber] process: %zu samples (%.2f s), peak=%.4f\n",
            pcmf32.size(), static_cast<double>(pcmf32.size()) / SR, peak);

    // VAD gate: skip if no speech detected in recent audio
    bool vad_pass = has_voice_activity(pcmf32, 1000, 0.6f);
    fprintf(stderr, "[transcriber] VAD: %s\n", vad_pass ? "PASS" : "skip");
    if (!vad_pass) return;

    // Run whisper inference
    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_progress   = false;
    wparams.print_special    = false;
    wparams.print_realtime   = false;
    wparams.print_timestamps = false;
    wparams.translate        = false;
    wparams.single_segment   = true;   // cleaner output for chunked streaming
    wparams.language         = "en";
    wparams.n_threads        = std::min(4, (int)std::thread::hardware_concurrency());

    // Break the hallucination feedback loop: don't feed prior output as prompt
    wparams.no_context       = true;

    // Suppress non-speech tokens: (music), (applause), (birds chirping), etc.
    wparams.suppress_nst     = true;

    // Bias the decoder toward lyric/speech output
    wparams.initial_prompt   = "Song lyrics transcription:";

    // Whisper's own no-speech gate
    wparams.no_speech_thold  = 0.6f;

    if (whisper_full(ctx_, wparams, pcmf32.data(), pcmf32.size()) != 0) {
        fprintf(stderr, "[transcriber] whisper_full() returned error\n");
        return;
    }

    int n_segs = whisper_full_n_segments(ctx_);
    fprintf(stderr, "[transcriber] got %d segment(s)\n", n_segs);

    for (int i = 0; i < n_segs; ++i) {
        const char* text        = whisper_full_get_segment_text(ctx_, i);
        float       no_speech_p = whisper_full_get_segment_no_speech_prob(ctx_, i);

        fprintf(stderr, "[transcriber] seg[%d] no_speech=%.3f raw='%s'\n",
                i, no_speech_p, text ? text : "(null)");

        if (!text || text[0] == '\0') continue;

        // Skip segments whisper itself flagged as likely non-speech
        if (no_speech_p > 0.6f) {
            fprintf(stderr, "[transcriber] seg[%d] dropped (no_speech_p %.3f > 0.6)\n",
                    i, no_speech_p);
            continue;
        }

        std::string seg(text);

        // Drop pure sound-annotation tokens: anything that is only "(...)" content
        // e.g. " (upbeat music)" or "[Music]"
        {
            std::string trimmed = seg;
            // strip leading whitespace
            size_t first = trimmed.find_first_not_of(" \t\n");
            if (first != std::string::npos) trimmed = trimmed.substr(first);
            bool is_annotation =
                (trimmed.size() >= 2 && trimmed.front() == '(' && trimmed.back() == ')') ||
                (trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']');
            if (is_annotation) {
                fprintf(stderr, "[transcriber] seg[%d] dropped (annotation token: '%s')\n",
                        i, trimmed.c_str());
                continue;
            }
        }

        // Trim leading whitespace before appending
        if (!seg.empty() && seg[0] == ' ') seg = seg.substr(1);
        if (seg.empty()) continue;

        pending_text_ += seg;

        if (is_sentence_end(pending_text_)) {
            emit(pending_text_, true);
            pending_text_.clear();
        }
    }

    // Emit partial text so the TUI stays live even mid-sentence
    if (!pending_text_.empty()) {
        emit(pending_text_, false);
    }
}
