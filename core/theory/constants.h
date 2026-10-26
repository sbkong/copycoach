// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <cstdint>

// Music conventions, single source: docs/dev/coding-standards.md §17. Changing any of these changes saved files.
namespace cc::theory
{

constexpr int firstString = 1; // string 1 is the highest-pitched string
constexpr int openFret = 0;
constexpr int pitchClassCount = 12; // 0 = C ... 11 = B
constexpr int middleC = 60; // MIDI note number of C4
constexpr int a4 = 69; // MIDI note number of A4 = 440 Hz
constexpr double a4Hz = 440.0;
constexpr std::int64_t ticksPerQuarter = 960;
constexpr double tempoMinBpm = 40.0;
constexpr double tempoMaxBpm = 220.0;
constexpr int maxVoices = 2;
constexpr int minStrings = 4;
constexpr int maxStrings = 7;

} // namespace cc::theory
