#include "transcriber.hpp"

#include <stdexcept>
#include <string>

#if defined(EVA_WITH_VOSK)
#include <vosk_api.h>
#endif

namespace {

#if defined(EVA_WITH_VOSK)
std::string extract_text_field(const std::string& json) {
    auto pos = json.find("\"text\"");
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos);
    if (pos == std::string::npos) return {};
    pos = json.find('"', pos);
    if (pos == std::string::npos) return {};
    auto end = json.find('"', pos + 1);
    if (end == std::string::npos) return {};
    return json.substr(pos + 1, end - pos - 1);
}
#endif

} // namespace

struct Transcriber::Impl {
#if defined(EVA_WITH_VOSK)
    VoskModel* model{nullptr};
    VoskRecognizer* recognizer{nullptr};
    bool ready{false};

    Impl(const std::string& model_path, int sample_rate) {
        model = vosk_model_new(model_path.c_str());
        if (!model) {
            throw std::runtime_error("Failed to load Vosk model at " + model_path);
        }
        recognizer = vosk_recognizer_new(model, sample_rate);
        if (!recognizer) {
            vosk_model_free(model);
            throw std::runtime_error("Failed to create Vosk recognizer");
        }
        vosk_recognizer_set_max_alternatives(recognizer, 0);
        vosk_recognizer_set_partial_words(recognizer, false);
        ready = true;
    }

    ~Impl() {
        if (recognizer) {
            vosk_recognizer_free(recognizer);
            recognizer = nullptr;
        }
        if (model) {
            vosk_model_free(model);
            model = nullptr;
        }
    }

    bool available() const { return ready; }

    void feed(const int16_t* data, std::size_t samples) {
        if (!ready) return;
        vosk_recognizer_accept_waveform(recognizer,
                                        reinterpret_cast<const char*>(data),
                                        static_cast<int>(samples * sizeof(int16_t)));
    }

    std::string flush() {
        if (!ready) return {};
        const char* raw = vosk_recognizer_final_result(recognizer);
        std::string json = raw ? raw : "";
        vosk_recognizer_reset(recognizer);
        return extract_text_field(json);
    }
#else
    Impl(const std::string&, int) {
        throw std::runtime_error("Vosk support not enabled; rebuild with EVA_ENABLE_VOSK=ON");
    }

    bool available() const { return false; }
    void feed(const int16_t*, std::size_t) {}
    std::string flush() { return {}; }
#endif
};

Transcriber::Transcriber(const std::string& model_path, int sample_rate)
    : impl_(std::make_unique<Impl>(model_path, sample_rate)) {}

Transcriber::~Transcriber() = default;

bool Transcriber::available() const { return impl_ && impl_->available(); }

void Transcriber::feed(const int16_t* data, std::size_t samples) {
    if (impl_) impl_->feed(data, samples);
}

std::string Transcriber::flush() {
    if (!impl_) return {};
    return impl_->flush();
}
