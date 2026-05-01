#include "audio.h"

#include <RtAudio.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <thread>

static constexpr unsigned int TARGET_RATE  = 16000;
static constexpr unsigned int CHUNK_FRAMES = 512;   // frames per RTAudio callback

// ---------------------------------------------------------------------------
// Device listing
// ---------------------------------------------------------------------------

std::vector<AudioDevice> list_input_devices() {
    RtAudio audio;
    std::vector<AudioDevice> result;
    for (unsigned int id : audio.getDeviceIds()) {
        auto info = audio.getDeviceInfo(id);
        if (info.inputChannels > 0) {
            result.push_back({id, info.name, info.inputChannels, info.isDefaultInput});
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// LiveAudioSource
// ---------------------------------------------------------------------------

struct LiveAudioSource::Impl {
    RtAudio          dac;
    AudioCallback    cb;
    unsigned int     channel    = 0;
    unsigned int     hwRate     = 0;
    unsigned int     nChannels  = 1;  // cached at stream open time

    // Simple linear resampler state
    double           resample_pos = 0.0;

    static int rt_callback(void* /*out*/, void* in, unsigned int nFrames,
                           double /*time*/, RtAudioStreamStatus /*status*/,
                           void* userData) {
        auto* impl = static_cast<Impl*>(userData);
        const float* src = static_cast<const float*>(in);

        // Extract the selected channel from interleaved input
        std::vector<float> mono(nFrames);
        for (unsigned int i = 0; i < nFrames; ++i) {
            mono[i] = src[i * impl->nChannels + impl->channel];
        }

        // Resample to TARGET_RATE using linear interpolation
        if (impl->hwRate == TARGET_RATE) {
            impl->cb(mono);
            return 0;
        }

        double ratio = static_cast<double>(TARGET_RATE) / impl->hwRate;
        unsigned int out_len = static_cast<unsigned int>(nFrames * ratio + 1);
        std::vector<float> resampled;
        resampled.reserve(out_len);

        for (unsigned int i = 0; i < out_len; ++i) {
            double idx = i / ratio;
            unsigned int i0 = static_cast<unsigned int>(idx);
            unsigned int i1 = std::min(i0 + 1, nFrames - 1);
            double frac = idx - i0;
            resampled.push_back(mono[i0] * (1.0 - frac) + mono[i1] * frac);
        }
        impl->resample_pos = 0;

        impl->cb(resampled);
        return 0;
    }
};

LiveAudioSource::LiveAudioSource() : impl_(new Impl) {}

LiveAudioSource::~LiveAudioSource() {
    stop();
    delete impl_;
}

bool LiveAudioSource::start(unsigned int deviceId, unsigned int channel, AudioCallback cb) {
    impl_->cb      = std::move(cb);
    impl_->channel = channel;

    auto info = impl_->dac.getDeviceInfo(deviceId);
    impl_->hwRate    = info.currentSampleRate > 0
                           ? info.currentSampleRate
                           : (info.sampleRates.empty() ? 44100 : info.sampleRates[0]);
    impl_->nChannels = info.inputChannels;

    RtAudio::StreamParameters params;
    params.deviceId     = deviceId;
    params.nChannels    = info.inputChannels;
    params.firstChannel = 0;

    unsigned int bufFrames = CHUNK_FRAMES;
    RtAudioErrorType err = impl_->dac.openStream(
        nullptr, &params, RTAUDIO_FLOAT32, impl_->hwRate,
        &bufFrames, &Impl::rt_callback, impl_);

    if (err != RTAUDIO_NO_ERROR) return false;

    err = impl_->dac.startStream();
    return err == RTAUDIO_NO_ERROR;
}

void LiveAudioSource::stop() {
    if (impl_->dac.isStreamRunning()) impl_->dac.stopStream();
    if (impl_->dac.isStreamOpen())    impl_->dac.closeStream();
}

// ---------------------------------------------------------------------------
// Minimal WAV reader (PCM 16-bit or 32-bit float, mono or stereo)
// Scans all RIFF chunks so it handles LIST/JUNK preambles correctly.
// ---------------------------------------------------------------------------

static bool read_wav_file(const std::string& path, std::vector<float>& out, uint32_t& rate) {
    fprintf(stderr, "[wav] opening: %s\n", path.c_str());

    std::ifstream f(path, std::ios::binary);
    if (!f) {
        fprintf(stderr, "[wav] ERROR: could not open file\n");
        return false;
    }

    // RIFF header
    char riff[4]{}, wave[4]{};
    uint32_t file_size = 0;
    f.read(riff, 4);
    f.read(reinterpret_cast<char*>(&file_size), 4);
    f.read(wave, 4);

    if (!f || strncmp(riff, "RIFF", 4) != 0) {
        fprintf(stderr, "[wav] ERROR: not a RIFF file (got '%.4s')\n", riff);
        return false;
    }
    if (strncmp(wave, "WAVE", 4) != 0) {
        fprintf(stderr, "[wav] ERROR: RIFF type is not WAVE (got '%.4s')\n", wave);
        return false;
    }
    fprintf(stderr, "[wav] RIFF/WAVE ok, declared file size: %u bytes\n", file_size);

    // Scan chunks for 'fmt ' and 'data'
    uint16_t audio_format    = 0;
    uint16_t num_channels    = 0;
    uint32_t sample_rate     = 0;
    uint16_t bits_per_sample = 0;
    bool     got_fmt  = false;
    bool     got_data = false;
    uint32_t data_size = 0;

    char chunk_id[4]{};
    uint32_t chunk_size = 0;

    while (f.read(chunk_id, 4) && f.read(reinterpret_cast<char*>(&chunk_size), 4)) {
        fprintf(stderr, "[wav] chunk '%.4s' size=%u\n", chunk_id, chunk_size);

        if (strncmp(chunk_id, "fmt ", 4) == 0) {
            if (chunk_size < 16) {
                fprintf(stderr, "[wav] ERROR: fmt chunk too small (%u bytes)\n", chunk_size);
                return false;
            }
            uint32_t byte_rate   = 0;
            uint16_t block_align = 0;
            f.read(reinterpret_cast<char*>(&audio_format),    2);
            f.read(reinterpret_cast<char*>(&num_channels),    2);
            f.read(reinterpret_cast<char*>(&sample_rate),     4);
            f.read(reinterpret_cast<char*>(&byte_rate),       4);
            f.read(reinterpret_cast<char*>(&block_align),     2);
            f.read(reinterpret_cast<char*>(&bits_per_sample), 2);
            // Skip any remaining fmt bytes (e.g. extensible format extra fields)
            if (chunk_size > 16) f.seekg(chunk_size - 16, std::ios::cur);
            got_fmt = true;
            fprintf(stderr, "[wav] fmt: format=%u channels=%u rate=%u bits=%u\n",
                    audio_format, num_channels, sample_rate, bits_per_sample);
        } else if (strncmp(chunk_id, "data", 4) == 0) {
            data_size = chunk_size;
            got_data  = true;
            break;  // PCM data follows immediately
        } else {
            // Unknown chunk — skip it
            f.seekg(chunk_size, std::ios::cur);
        }
    }

    if (!got_fmt) {
        fprintf(stderr, "[wav] ERROR: no 'fmt ' chunk found\n");
        return false;
    }
    if (!got_data) {
        fprintf(stderr, "[wav] ERROR: no 'data' chunk found\n");
        return false;
    }
    if (!f) {
        fprintf(stderr, "[wav] ERROR: stream bad after chunk scan\n");
        return false;
    }

    if (num_channels == 0) {
        fprintf(stderr, "[wav] ERROR: num_channels is 0\n");
        return false;
    }
    if (bits_per_sample == 0) {
        fprintf(stderr, "[wav] ERROR: bits_per_sample is 0\n");
        return false;
    }

    unsigned int bytes_per_sample = bits_per_sample / 8;
    unsigned int total_samples    = data_size / bytes_per_sample;
    unsigned int total_frames     = total_samples / num_channels;
    double duration_s             = static_cast<double>(total_frames) / sample_rate;

    fprintf(stderr, "[wav] data: %u bytes, %u frames, %.2f seconds\n",
            data_size, total_frames, duration_s);

    rate = sample_rate;
    out.resize(total_frames);

    if (audio_format == 3 && bits_per_sample == 32) {
        fprintf(stderr, "[wav] reading float32 PCM\n");
        std::vector<float> raw(total_samples);
        f.read(reinterpret_cast<char*>(raw.data()),
               static_cast<std::streamsize>(data_size));
        if (!f) {
            fprintf(stderr, "[wav] ERROR: short read on float32 data (expected %u bytes)\n", data_size);
            return false;
        }
        for (unsigned int i = 0; i < total_frames; ++i) {
            float s = 0;
            for (unsigned int c = 0; c < num_channels; ++c)
                s += raw[i * num_channels + c];
            out[i] = s / num_channels;
        }
    } else if (audio_format == 1 && bits_per_sample == 16) {
        fprintf(stderr, "[wav] reading int16 PCM\n");
        std::vector<int16_t> raw(total_samples);
        f.read(reinterpret_cast<char*>(raw.data()),
               static_cast<std::streamsize>(data_size));
        if (!f) {
            fprintf(stderr, "[wav] ERROR: short read on int16 data (expected %u bytes)\n", data_size);
            return false;
        }
        for (unsigned int i = 0; i < total_frames; ++i) {
            float s = 0;
            for (unsigned int c = 0; c < num_channels; ++c)
                s += raw[i * num_channels + c] / 32768.0f;
            out[i] = s / num_channels;
        }
    } else if (audio_format == 1 && bits_per_sample == 24) {
        fprintf(stderr, "[wav] reading int24 PCM\n");
        std::vector<uint8_t> raw(data_size);
        f.read(reinterpret_cast<char*>(raw.data()),
               static_cast<std::streamsize>(data_size));
        if (!f) {
            fprintf(stderr, "[wav] ERROR: short read on int24 data\n");
            return false;
        }
        for (unsigned int i = 0; i < total_frames; ++i) {
            float s = 0;
            for (unsigned int c = 0; c < num_channels; ++c) {
                unsigned int byte_offset = (i * num_channels + c) * 3;
                int32_t sample = (raw[byte_offset + 2] << 16)
                               | (raw[byte_offset + 1] <<  8)
                               |  raw[byte_offset];
                if (sample & 0x800000) sample |= ~0xFFFFFF;  // sign-extend
                s += sample / 8388608.0f;
            }
            out[i] = s / num_channels;
        }
    } else {
        fprintf(stderr, "[wav] ERROR: unsupported format: audio_format=%u bits=%u\n",
                audio_format, bits_per_sample);
        return false;
    }

    fprintf(stderr, "[wav] loaded %zu mono samples at %u Hz\n", out.size(), sample_rate);
    return true;
}

// Resample mono float buffer from src_rate to TARGET_RATE
static std::vector<float> resample(const std::vector<float>& src, uint32_t src_rate) {
    fprintf(stderr, "[resample] input: %zu samples @ %u Hz\n", src.size(), src_rate);
    if (src_rate == TARGET_RATE) {
        fprintf(stderr, "[resample] no-op (already at %u Hz)\n", TARGET_RATE);
        return src;
    }
    double ratio = static_cast<double>(TARGET_RATE) / src_rate;
    size_t out_len = static_cast<size_t>(src.size() * ratio);
    fprintf(stderr, "[resample] ratio=%.4f -> %zu samples @ %u Hz (%.2f s)\n",
            ratio, out_len, TARGET_RATE,
            static_cast<double>(out_len) / TARGET_RATE);
    std::vector<float> out(out_len);
    for (size_t i = 0; i < out_len; ++i) {
        double idx = i / ratio;
        size_t i0 = static_cast<size_t>(idx);
        size_t i1 = std::min(i0 + 1, src.size() - 1);
        double frac = idx - i0;
        out[i] = src[i0] * (1.0 - frac) + src[i1] * frac;
    }
    return out;
}

// ---------------------------------------------------------------------------
// DemoAudioSource
// ---------------------------------------------------------------------------

DemoAudioSource::DemoAudioSource()  = default;
DemoAudioSource::~DemoAudioSource() { stop(); }

bool DemoAudioSource::load(const std::string& wav_path) {
    uint32_t rate = 0;
    std::vector<float> raw;
    fprintf(stderr, "[demo] loading WAV: %s\n", wav_path.c_str());
    if (!read_wav_file(wav_path, raw, rate)) {
        fprintf(stderr, "[demo] ERROR: WAV load failed\n");
        return false;
    }
    samples_ = resample(raw, rate);
    fprintf(stderr, "[demo] ready: %zu samples (%.1f s) at %u Hz\n",
            samples_.size(), static_cast<double>(samples_.size()) / TARGET_RATE, TARGET_RATE);
    return true;
}

bool DemoAudioSource::start(unsigned int, unsigned int, AudioCallback cb) {
    if (samples_.empty()) {
        fprintf(stderr, "[demo] ERROR: start() called but no samples loaded\n");
        return false;
    }
    fprintf(stderr, "[demo] starting playback of %zu samples in 100 ms chunks\n",
            samples_.size());
    running_ = true;
    std::thread([this, cb = std::move(cb)]() {
        constexpr unsigned int CHUNK = TARGET_RATE / 10;  // 100 ms chunks
        size_t pos = 0;
        unsigned int report_interval = TARGET_RATE;  // log every ~1 second of audio
        size_t next_report = report_interval;
        while (running_ && pos < samples_.size()) {
            size_t end = std::min(pos + CHUNK, samples_.size());
            cb(std::vector<float>(samples_.begin() + pos, samples_.begin() + end));
            pos = end;
            if (pos >= next_report) {
                fprintf(stderr, "[demo] playback position: %.1f / %.1f s\n",
                        static_cast<double>(pos) / TARGET_RATE,
                        static_cast<double>(samples_.size()) / TARGET_RATE);
                next_report += report_interval;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        fprintf(stderr, "[demo] playback finished (pos=%zu total=%zu)\n",
                pos, samples_.size());
        running_ = false;
    }).detach();
    return true;
}

void DemoAudioSource::stop() {
    running_ = false;
}
