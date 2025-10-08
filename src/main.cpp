#include "audio_capture.hpp"
#include "transcriber.hpp"
#include "utils.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace {
bool g_running = true;

void handle_sigint(int) {
    g_running = false;
}
} // namespace

int main() {
    std::signal(SIGINT, handle_sigint);

    std::cout << "Listing input devices...\n";
    AudioCapture::list_devices();

    AudioConfig cfg;
    cfg.frames_per_buffer = 512;

    AudioCapture capture(cfg);
    try {
        capture.start();
    } catch (const std::exception& e) {
        std::cerr << "Failed to start audio capture: " << e.what() << "\n";
        return 1;
    }

    const char* model_env = std::getenv("EVA_VOSK_MODEL");
    std::string model_path = model_env ? model_env : "models/vosk-model-small-en-us-0.15";
    bool transcription_enabled = true;
    std::unique_ptr<Transcriber> transcriber;
    try {
        transcriber = std::make_unique<Transcriber>(model_path, static_cast<int>(cfg.sample_rate));
        transcription_enabled = transcriber->available();
        if (transcription_enabled) {
            std::cout << "Transcription enabled using model: " << model_path << "\n";
        } else {
            std::cout << "Transcription unavailable (model not ready).\n";
        }
    } catch (const std::exception& e) {
        std::cout << "Transcription disabled: " << e.what() << "\n";
        transcription_enabled = false;
    }

    std::vector<int16_t> buffer;

    const float trigger_dbfs = -35.0f;
    const int trigger_frames = 10;
    const int release_frames = 20;

    int hot = 0;
    int hold = 0;
    bool segment_active = false;
    bool segment_has_audio = false;

    std::cout << "\nRunning... Press Ctrl+C to quit.\n";
    while (g_running) {
        if (capture.read(buffer) == 0) continue;

        float level = dbfs(buffer);
        bool speech_like = (level > trigger_dbfs);

        if (speech_like) {
            hot++;
            if (hot >= trigger_frames) {
                hold = release_frames;
                hot = 0;
                std::cout << "[VAD] Speech detected (" << level << " dBFS)\n";
            }
        } else {
            hot = 0;
        }

        if (hold > 0) {
            hold--;
        }

        if ((speech_like || hold > 0) && transcription_enabled) {
            segment_active = true;
            segment_has_audio = true;
            transcriber->feed(buffer.data(), buffer.size());
        }

        if (!speech_like && hold == 0 && segment_active) {
            segment_active = false;
            if (transcription_enabled && segment_has_audio) {
                std::string text = transcriber->flush();
                if (!text.empty()) {
                    std::cout << "[Transcription] " << text << "\n";
                } else {
                    std::cout << "[Transcription] (no speech recognised)\n";
                }
            }
            segment_has_audio = false;
        }
    }

    std::cout << "Exiting.\n";
    return 0;
}
