// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/predict/modelfile.h"

#include <string_view>

// The Spleeter file the app downloads at first use (states.md section 2 "모델 가중치 다운로드"), exported by
// tools/models/export_spleeter.py from Deezer's official 4stems checkpoint (release v1.4.0; MIT per the JOSS
// paper, README names only the code: copy-spec.md section 4 "스템 분리 조사") and hosted on the project's
// GitHub release; size and SHA-256 come from the export manifest (2026-11-10). Not bundled with the app.
// The htdemucs file has no entry here on purpose: research-only weights the app never distributes.
namespace cc::separation
{

constexpr std::string_view spleeterReleaseUrl =
    "https://github.com/sbkong/copycoach/releases/download/models-spleeter-1/";
constexpr std::string_view spleeterLicense = "MIT (deezer/spleeter, JOSS paper)";

constexpr predict::ModelFile spleeterModel{"spleeter-4stems.onnx",
                                           157284697,
                                           "c6980a7c6680fcc33f932181ab794e03a08aeb8bbcd76fd630e568d544cde756"};

constexpr std::string_view htdemucsFileName = "htdemucs.onnx"; // the user's own conversion, any size

} // namespace cc::separation
