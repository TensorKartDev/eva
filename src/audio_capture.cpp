#include "audio_capture.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__APPLE__)

#include <AudioToolbox/AudioQueue.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>

namespace {

std::string cf_string_to_std(CFStringRef ref) {
    if (!ref) return {};
    char buffer[256];
    if (CFStringGetCString(ref, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
        return std::string(buffer);
    }
    return {};
}

std::string format_osstatus(OSStatus status, const std::string& context) {
    return context + " (osstatus=" + std::to_string(status) + ")";
}

} // namespace

struct AudioCapture::Impl {
    explicit Impl(const AudioConfig& cfg);
    ~Impl();

    void start();
    std::size_t read(std::vector<int16_t>& out);
    static void list_devices();

private:
    static void audio_callback(void* user_data,
                               AudioQueueRef in_aq,
                               AudioQueueBufferRef in_buffer,
                               const AudioTimeStamp*,
                               UInt32,
                               const AudioStreamPacketDescription*);

    AudioConfig cfg_;
    AudioQueueRef queue_{nullptr};
    std::vector<AudioQueueBufferRef> buffers_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::vector<int16_t>> pending_;
    bool running_{false};
};

AudioCapture::Impl::Impl(const AudioConfig& cfg) : cfg_(cfg) {}

AudioCapture::Impl::~Impl() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    cv_.notify_all();

    if (queue_) {
        AudioQueueStop(queue_, true);
        AudioQueueDispose(queue_, true);
        queue_ = nullptr;
    }
}

void AudioCapture::Impl::audio_callback(void* user_data,
                                        AudioQueueRef in_aq,
                                        AudioQueueBufferRef in_buffer,
                                        const AudioTimeStamp*,
                                        UInt32,
                                        const AudioStreamPacketDescription*) {
    auto* self = static_cast<AudioCapture::Impl*>(user_data);
    if (!self->running_) {
        AudioQueueEnqueueBuffer(in_aq, in_buffer, 0, nullptr);
        return;
    }

    std::size_t samples = in_buffer->mAudioDataByteSize / sizeof(int16_t);
    if (samples > 0) {
        std::vector<int16_t> copy(samples);
        std::memcpy(copy.data(), in_buffer->mAudioData, in_buffer->mAudioDataByteSize);
        {
            std::lock_guard<std::mutex> lock(self->mutex_);
            self->pending_.push_back(std::move(copy));
        }
        self->cv_.notify_one();
    }

    AudioQueueEnqueueBuffer(in_aq, in_buffer, 0, nullptr);
}

void AudioCapture::Impl::start() {
    if (running_) return;

    AudioStreamBasicDescription format{};
    format.mSampleRate = cfg_.sample_rate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    format.mBitsPerChannel = 16;
    format.mChannelsPerFrame = cfg_.channels;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = (format.mBitsPerChannel / 8) * format.mChannelsPerFrame;
    format.mBytesPerPacket = format.mBytesPerFrame;

    OSStatus status = AudioQueueNewInput(&format,
                                         AudioCapture::Impl::audio_callback,
                                         this,
                                         nullptr,
                                         nullptr,
                                         0,
                                         &queue_);
    if (status != noErr) {
        throw std::runtime_error(format_osstatus(status, "AudioQueueNewInput failed"));
    }

    std::size_t buffer_bytes = static_cast<std::size_t>(cfg_.frames_per_buffer) * format.mBytesPerFrame;
    if (buffer_bytes == 0) {
        throw std::runtime_error("Invalid frames_per_buffer for capture");
    }

    buffers_.resize(3);
    for (auto& buffer : buffers_) {
        status = AudioQueueAllocateBuffer(queue_, static_cast<UInt32>(buffer_bytes), &buffer);
        if (status != noErr) {
            throw std::runtime_error(format_osstatus(status, "AudioQueueAllocateBuffer failed"));
        }
        buffer->mAudioDataByteSize = static_cast<UInt32>(buffer_bytes);
        std::memset(buffer->mAudioData, 0, buffer->mAudioDataByteSize);
        status = AudioQueueEnqueueBuffer(queue_, buffer, 0, nullptr);
        if (status != noErr) {
            throw std::runtime_error(format_osstatus(status, "AudioQueueEnqueueBuffer failed"));
        }
    }

    status = AudioQueueStart(queue_, nullptr);
    if (status != noErr) {
        throw std::runtime_error(format_osstatus(status, "AudioQueueStart failed"));
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = true;
    }
}

std::size_t AudioCapture::Impl::read(std::vector<int16_t>& out) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [&] { return !pending_.empty() || !running_; });

    if (!running_ && pending_.empty()) {
        return 0;
    }

    std::vector<int16_t> chunk = std::move(pending_.front());
    pending_.pop_front();
    lock.unlock();

    out = std::move(chunk);
    return out.size();
}

void AudioCapture::Impl::list_devices() {
    AudioObjectPropertyAddress devices_addr{
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain};

    UInt32 data_size = 0;
    OSStatus status = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &devices_addr, 0, nullptr, &data_size);
    if (status != noErr || data_size == 0) {
        std::cout << "No CoreAudio devices found.\n";
        return;
    }

    std::vector<AudioDeviceID> devices(data_size / sizeof(AudioDeviceID));
    status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &devices_addr, 0, nullptr, &data_size, devices.data());
    if (status != noErr) {
        std::cout << "Unable to enumerate CoreAudio devices (status " << status << ").\n";
        return;
    }

    std::cout << "CoreAudio input devices:\n";
    for (AudioDeviceID device : devices) {
        AudioObjectPropertyAddress stream_addr{
            kAudioDevicePropertyStreamConfiguration,
            kAudioDevicePropertyScopeInput,
            kAudioObjectPropertyElementMain};

        UInt32 cfg_size = 0;
        status = AudioObjectGetPropertyDataSize(device, &stream_addr, 0, nullptr, &cfg_size);
        if (status != noErr || cfg_size == 0) continue;

        auto buffer_list = static_cast<AudioBufferList*>(std::malloc(cfg_size));
        if (!buffer_list) continue;

        status = AudioObjectGetPropertyData(device, &stream_addr, 0, nullptr, &cfg_size, buffer_list);
        if (status != noErr) {
            std::free(buffer_list);
            continue;
        }

        UInt32 total_channels = 0;
        for (UInt32 i = 0; i < buffer_list->mNumberBuffers; ++i) {
            total_channels += buffer_list->mBuffers[i].mNumberChannels;
        }
        std::free(buffer_list);

        if (total_channels == 0) continue;

        CFStringRef name_ref = nullptr;
        UInt32 name_size = sizeof(name_ref);
        AudioObjectPropertyAddress name_addr{
            kAudioObjectPropertyName,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain};
        status = AudioObjectGetPropertyData(device, &name_addr, 0, nullptr, &name_size, &name_ref);

        std::string name = (status == noErr) ? cf_string_to_std(name_ref) : std::string{};
        if (name_ref) {
            CFRelease(name_ref);
        }

        if (name.empty()) name = "Unknown device";
        std::cout << "- ID " << device << ": " << name << " (" << total_channels << "ch)\n";
    }
    std::cout << "(Use default input device; set via System Settings.)\n";
}

#elif defined(__linux__)

#include <alsa/asoundlib.h>
#include <cstdio>
#include <sstream>

namespace {

std::string alsa_error(int code, const std::string& context) {
    std::ostringstream oss;
    oss << context << ": " << snd_strerror(code);
    return oss.str();
}

} // namespace

struct AudioCapture::Impl {
    explicit Impl(const AudioConfig& cfg);
    ~Impl();

    void start();
    std::size_t read(std::vector<int16_t>& out);
    static void list_devices();

private:
    AudioConfig cfg_;
    snd_pcm_t* handle_{nullptr};
    bool started_{false};
};

AudioCapture::Impl::Impl(const AudioConfig& cfg) : cfg_(cfg) {}

AudioCapture::Impl::~Impl() {
    if (handle_) {
        snd_pcm_drop(handle_);
        snd_pcm_close(handle_);
        handle_ = nullptr;
    }
}

void AudioCapture::Impl::start() {
    if (started_) return;

    int err = snd_pcm_open(&handle_, cfg_.device.c_str(), SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        throw std::runtime_error(alsa_error(err, "snd_pcm_open"));
    }

    snd_pcm_hw_params_t* hw_params = nullptr;
    snd_pcm_hw_params_malloc(&hw_params);
    if (!hw_params) {
        throw std::runtime_error("Failed to allocate ALSA hw params");
    }

    snd_pcm_hw_params_any(handle_, hw_params);

    err = snd_pcm_hw_params_set_access(handle_, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0) {
        snd_pcm_hw_params_free(hw_params);
        throw std::runtime_error(alsa_error(err, "snd_pcm_hw_params_set_access"));
    }

    err = snd_pcm_hw_params_set_format(handle_, hw_params, SND_PCM_FORMAT_S16_LE);
    if (err < 0) {
        snd_pcm_hw_params_free(hw_params);
        throw std::runtime_error(alsa_error(err, "snd_pcm_hw_params_set_format"));
    }

    err = snd_pcm_hw_params_set_channels(handle_, hw_params, cfg_.channels);
    if (err < 0) {
        snd_pcm_hw_params_free(hw_params);
        throw std::runtime_error(alsa_error(err, "snd_pcm_hw_params_set_channels"));
    }

    unsigned int rate = cfg_.sample_rate;
    err = snd_pcm_hw_params_set_rate_near(handle_, hw_params, &rate, nullptr);
    if (err < 0) {
        snd_pcm_hw_params_free(hw_params);
        throw std::runtime_error(alsa_error(err, "snd_pcm_hw_params_set_rate_near"));
    }
    if (rate != cfg_.sample_rate) {
        std::cout << "Warning: sample rate adjusted to " << rate << " Hz\n";
        cfg_.sample_rate = rate;
    }

    snd_pcm_uframes_t frames = cfg_.frames_per_buffer;
    err = snd_pcm_hw_params_set_period_size_near(handle_, hw_params, &frames, nullptr);
    if (err < 0) {
        snd_pcm_hw_params_free(hw_params);
        throw std::runtime_error(alsa_error(err, "snd_pcm_hw_params_set_period_size_near"));
    }
    cfg_.frames_per_buffer = static_cast<unsigned>(frames);

    err = snd_pcm_hw_params(handle_, hw_params);
    snd_pcm_hw_params_free(hw_params);
    if (err < 0) {
        throw std::runtime_error(alsa_error(err, "snd_pcm_hw_params"));
    }

    err = snd_pcm_prepare(handle_);
    if (err < 0) {
        throw std::runtime_error(alsa_error(err, "snd_pcm_prepare"));
    }

    started_ = true;
}

std::size_t AudioCapture::Impl::read(std::vector<int16_t>& out) {
    if (!handle_) return 0;

    out.resize(static_cast<std::size_t>(cfg_.frames_per_buffer) * cfg_.channels);
    snd_pcm_sframes_t frames = snd_pcm_readi(handle_, out.data(), cfg_.frames_per_buffer);
    if (frames < 0) {
        frames = snd_pcm_recover(handle_, frames, 1);
    }
    if (frames < 0) {
        throw std::runtime_error(alsa_error(static_cast<int>(frames), "snd_pcm_readi"));
    }
    if (frames == 0) {
        out.clear();
        return 0;
    }

    std::size_t samples = static_cast<std::size_t>(frames) * cfg_.channels;
    out.resize(samples);
    return samples;
}

void AudioCapture::Impl::list_devices() {
    int card = -1;
    if (snd_card_next(&card) < 0 || card < 0) {
        std::cout << "No ALSA capture devices found.\n";
        return;
    }

    std::cout << "ALSA capture devices (use \"plughw:x,y\"):\n";
    while (card >= 0) {
        snd_ctl_t* ctl = nullptr;
        char card_name[32];
        std::snprintf(card_name, sizeof(card_name), "hw:%d", card);
        if (snd_ctl_open(&ctl, card_name, 0) < 0) {
            snd_card_next(&card);
            continue;
        }

        snd_pcm_info_t* pcm_info = nullptr;
        snd_pcm_info_malloc(&pcm_info);
        if (!pcm_info) {
            std::cout << "  (Failed to allocate pcm_info)\n";
            snd_ctl_close(ctl);
            snd_card_next(&card);
            continue;
        }

        int device = -1;
        while (true) {
            if (snd_ctl_pcm_next_device(ctl, &device) < 0) break;
            if (device < 0) break;

            snd_pcm_info_set_device(pcm_info, device);
            snd_pcm_info_set_subdevice(pcm_info, 0);
            snd_pcm_info_set_stream(pcm_info, SND_PCM_STREAM_CAPTURE);

            if (snd_ctl_pcm_info(ctl, pcm_info) < 0) continue;

            const char* name = snd_pcm_info_get_name(pcm_info);
            const char* id = snd_pcm_info_get_id(pcm_info);
            std::cout << "- hw:" << card << "," << device;
            if (name) std::cout << " (" << name << ")";
            if (id) std::cout << " [" << id << "]";
            std::cout << "\n";
        }

        snd_pcm_info_free(pcm_info);
        snd_ctl_close(ctl);
        snd_card_next(&card);
    }
}

#else

struct AudioCapture::Impl {
    explicit Impl(const AudioConfig&) {
        throw std::runtime_error("Audio capture not supported on this platform");
    }
    ~Impl() = default;
    void start() {}
    std::size_t read(std::vector<int16_t>&) { return 0; }
    static void list_devices() {
        std::cout << "Audio capture not supported on this platform.\n";
    }
};

#endif

AudioCapture::AudioCapture(const AudioConfig& cfg) : impl_(new Impl(cfg)) {}

AudioCapture::~AudioCapture() { delete impl_; }

void AudioCapture::start() { impl_->start(); }

std::size_t AudioCapture::read(std::vector<int16_t>& out) { return impl_->read(out); }

void AudioCapture::list_devices() { Impl::list_devices(); }
