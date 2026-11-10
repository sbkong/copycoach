// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <cstdint>
#include <string_view>

// One downloadable model file as its export manifest describes it (states.md section 2 "모델 가중치 다운로드"):
// the app checks the size on every use and the SHA-256 once, when the download lands.
namespace cc::predict
{

struct ModelFile {
    std::string_view name;
    std::uint64_t size;
    std::string_view sha256;
};

} // namespace cc::predict
