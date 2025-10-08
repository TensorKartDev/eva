#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

class Transcriber {
public:
    Transcriber(const std::string& model_path, int sample_rate);
    ~Transcriber();

    bool available() const;

    void feed(const int16_t* data, std::size_t samples);
    std::string flush();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
