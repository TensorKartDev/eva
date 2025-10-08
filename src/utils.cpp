#include "utils.hpp"

#include <cmath>

float rms(const std::vector<int16_t>& x) {
    if (x.empty()) return 0.0f;
    double acc = 0.0;
    for (auto sample : x) {
        acc += static_cast<double>(sample) * static_cast<double>(sample);
    }
    double mean = acc / static_cast<double>(x.size());
    return static_cast<float>(std::sqrt(mean));
}

float dbfs(const std::vector<int16_t>& x) {
    const float r = rms(x);
    const float ref = 32768.0f; // int16 max magnitude
    return 20.0f * std::log10((r + 1e-9f) / ref);
}
