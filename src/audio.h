#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

// Describes one RTAudio input device.
struct AudioDevice {
    unsigned int id;
    std::string  name;
    unsigned int inputChannels;
    bool         isDefaultInput;
};

// Lists all devices with at least one input channel.
std::vector<AudioDevice> list_input_devices();

// Callback type: called whenever new PCM samples are available.
// Samples are mono float32 at WHISPER_SAMPLE_RATE (16 kHz).
using AudioCallback = std::function<void(const std::vector<float>&)>;

//
// AudioSource — base class for live and demo sources.
//
class AudioSource {
public:
    virtual ~AudioSource() = default;
    virtual bool start(unsigned int deviceId, unsigned int channel, AudioCallback cb) = 0;
    virtual void stop() = 0;
};

// Live microphone capture via RTAudio. Resamples to 16 kHz mono.
class LiveAudioSource : public AudioSource {
public:
    LiveAudioSource();
    ~LiveAudioSource() override;
    bool start(unsigned int deviceId, unsigned int channel, AudioCallback cb) override;
    void stop() override;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

// Demo mode: plays back a WAV file at realtime rate, chunked to simulate live input.
class DemoAudioSource : public AudioSource {
public:
    DemoAudioSource();
    ~DemoAudioSource() override;
    bool start(unsigned int /*deviceId*/, unsigned int /*channel*/, AudioCallback cb) override;
    void stop() override;

    bool load(const std::string& wav_path);

private:
    std::vector<float> samples_;  // 16 kHz mono
    std::atomic<bool>  running_{false};
};
