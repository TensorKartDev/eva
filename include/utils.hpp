#pragma once

#include <cstdint>
#include <vector>

float rms(const std::vector<int16_t>& x);
float dbfs(const std::vector<int16_t>& x);
