// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string_view>

// Stem separation (copy-spec.md section 4 "스템", section 16.2 D-2): the stems a model delivers, in the app's
// fixed order, at the cache rate. A four-stem model (Spleeter 4stems, htdemucs) delivers the first four;
// htdemucs_6s adds guitar and piano (user 2026-11-17: a guitarist cannot copy from a guitar buried in
// "other"). An engine streams finished stereo frames to a sink per stem so a whole song never sits in memory
// once per stem; the separator writes them as FLAC under the project's stems/.
namespace cc::separation
{

constexpr int stemSampleRate = 44100;
constexpr int stemChannels = 2;
constexpr std::size_t baseStemCount = 4;
constexpr std::size_t maxStemCount = 6;
constexpr std::array<std::string_view, maxStemCount> stemNames{"vocals", "drums", "bass", "other", "guitar", "piano"};
constexpr std::size_t otherStem = 3; // where a four-stem model leaves the guitar
constexpr std::size_t guitarStem = 4;

/// The sizes the app plays as a stem set: the first four or all six.
constexpr bool isStemSet(std::size_t count)
{
    return count == baseStemCount || count == maxStemCount;
}

enum class StemEngine : std::uint8_t {
    Spleeter, // the public path: Deezer 4stems, MIT, downloaded from the project's release
    Htdemucs, // research-only weights (Meta): never shipped, used when the user put a file in models/htdemucs/
    Roformer, // BS-RoFormer-SW, six stems, provenance unknown: never shipped, the user's own models/bs_roformer/
};

struct StemModel {
    StemEngine engine = StemEngine::Spleeter;
    std::filesystem::path path;
};

/// Called with interleaved stereo 44.1 kHz frames of one stem (index into stemNames), in order, possibly
/// many times per stem. Returning an error stops the separation.
using StemSink = std::function<Result<void>(std::size_t stem, std::span<const float> interleaved)>;

} // namespace cc::separation
