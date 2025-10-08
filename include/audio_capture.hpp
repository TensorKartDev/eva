#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct AudioConfig {
    unsigned sample_rate = 16000;          // 16 kHz for keyword spotting
    unsigned channels = 1;                 // mono by default
    unsigned frames_per_buffer = 512;      // buffer size per capture callback
    std::string device = "default";        // ALSA device name or default input
};

class AudioCapture {
public:
    explicit AudioCapture(const AudioConfig& cfg);
    ~AudioCapture();

    void start();
    std::size_t read(std::vector<int16_t>& out);

    static void list_devices();

private:
    struct Impl;
    Impl* impl_;
};
