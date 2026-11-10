// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/predict/modelfile.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// The Beat This! files the app downloads at first use (states.md section 2 "모델 가중치 다운로드"), exported by
// tools/models/export_beat_this.py from the official MIT checkpoints and hosted on the project's GitHub
// release; sizes and SHA-256 come from the export manifest (2026-11-05). Not bundled with the app.
namespace cc::predict
{

constexpr std::string_view beatThisReleaseUrl =
    "https://github.com/sbkong/copycoach/releases/download/models-beat-this-1/";
constexpr std::string_view beatThisLicense = "MIT (CPJKU/beat_this)";

constexpr ModelFile beatThisFilterbank{"mel-filterbank.bin",
                                       262656,
                                       "1ee975d96f44ccf2c3bfe37825c1c1f0b089f5703c7a12a84b1f0a3bce004533"};
constexpr ModelFile beatThisFinal{"beat_this-final0.onnx",
                                  83162650,
                                  "010f82d7ca3d1dc67f49e53c2e988e5a1d7572476d961c0d3d779cff230fc522"};
constexpr ModelFile beatThisSmall{"beat_this-small0.onnx",
                                  10555592,
                                  "f206462ff75d51178aba5ec6f05e881fb2ab8f254b2ec403f1d7073232ae329a"};

enum class BeatThisVariant : std::uint8_t {
    Final, // the paper's model, 83 MB
    Small, // 10 MB, F >= 0.99 of the full one: the low-end fallback (user decision 2026-11-05)
};

constexpr const ModelFile &beatThisModel(BeatThisVariant variant)
{
    return variant == BeatThisVariant::Small ? beatThisSmall : beatThisFinal;
}

/// Everything a variant needs on disk.
constexpr std::array<ModelFile, 2> beatThisFiles(BeatThisVariant variant)
{
    return {beatThisModel(variant), beatThisFilterbank};
}

} // namespace cc::predict
