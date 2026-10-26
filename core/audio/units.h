// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <cmath>

// Level conversions, single place (coding-standards.md section 18). Full scale is 1.0 = 0 dBFS.
namespace cc::audio
{

constexpr float silenceDb = -120.0F; // what gainToDb reports for zero

inline float dbToGain(float db)
{
    return std::pow(10.0F, db / 20.0F);
}

inline float gainToDb(float gain)
{
    return gain > 0.0F ? 20.0F * std::log10(gain) : silenceDb;
}

/// Soft clip above the knee: transparent below `knee`, tanh-shaped above, never exceeds 1.0 in magnitude.
inline float softClip(float x, float knee = 0.9F)
{
    const float magnitude = std::abs(x);
    if (magnitude <= knee) {
        return x;
    }
    const float over = (magnitude - knee) / (1.0F - knee);
    const float shaped = knee + ((1.0F - knee) * std::tanh(over));
    return x < 0.0F ? -shaped : shaped;
}

} // namespace cc::audio
