// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/predict/modelfile.h"

#include <string_view>

// The ChordMini file the app downloads at first use (states.md section 2 "모델 가중치 다운로드"), exported by
// tools/models/export_chordmini.py from the official MIT checkpoint (ChordNet 2E1D student, ptnghia-j/ChordMini)
// and hosted on the project's GitHub release; size and SHA-256 come from the export manifest (2026-11-10).
// Not bundled with the app. The BTC teacher weights are not used (licence dispute, copy-spec.md section 4).
namespace cc::predict
{

constexpr std::string_view chordMiniReleaseUrl =
    "https://github.com/sbkong/copycoach/releases/download/models-chordmini-1/";
constexpr std::string_view chordMiniLicense = "MIT (ptnghia-j/ChordMini)";

constexpr ModelFile chordMiniModel{"chordmini-2e1d.onnx",
                                   9604766,
                                   "44fc91637dcb6abe78e5518bbf90d31a6fbca45f4c66c1e629abff41a5b4fad1"};

} // namespace cc::predict
