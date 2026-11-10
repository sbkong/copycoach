// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// HT-Demucs host side (copy-spec.md section 4 "스템 분리 조사" path (나)): the transition weights of
// demucs.apply, and, when a waveform-in ONNX file sits in the local research folder, the fixture mix comes
// out as four finite stems of the right length that roughly add up to the mix.
#include "core/separation/htdemucs.h"
#include "core/separation/spleetermodel.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace cc;
using namespace cc::separation;

namespace
{

const std::filesystem::path s_model = std::filesystem::path(CC_HTDEMUCS_DIR) / htdemucsFileName;
const std::filesystem::path s_fixtureDir = std::filesystem::path(CC_FIXTURE_DIR) / "separation";

std::vector<float> readFloats(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file);
    file.seekg(0, std::ios::end);
    const auto bytes = static_cast<std::size_t>(file.tellg());
    file.seekg(0);
    std::vector<float> out(bytes / sizeof(float));
    file.read(reinterpret_cast<char *>(out.data()), // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
              static_cast<std::streamsize>(bytes));
    return out;
}

} // namespace

TEST_CASE("htdemucs: transition weights rise to the middle and fall back, peak 1")
{
    const std::vector<float> w = htdemucsWeights(8);
    REQUIRE(w.size() == 8);
    CHECK(w[0] == 0.25F);
    CHECK(w[3] == 1.0F);
    CHECK(w[4] == 1.0F);
    CHECK(w[7] == 0.25F);
    CHECK(htdemucsStride == 257985);
}

TEST_CASE("htdemucs: a local research model separates the fixture mix into four stems")
{
    if (!std::filesystem::exists(s_model)) {
        SKIP("htdemucs.onnx not present (research-only weights, never downloaded by the app)");
    }
    const std::vector<float> mix = readFloats(s_fixtureDir / "spleeter-mix.f32");
    std::array<std::vector<float>, stemCount> stems;
    const StemSink sink = [&stems](std::size_t stem, std::span<const float> interleaved) -> Result<void> {
        stems[stem].insert(stems[stem].end(), interleaved.begin(), interleaved.end());
        return {};
    };
    const std::stop_token never;
    const Result<void> r = separateHtdemucs(mix, s_model, sink, never, [](double) { });
    REQUIRE(r);
    double sumError = 0.0;
    double mixEnergy = 0.0;
    for (std::size_t i = 0; i < mix.size(); ++i) {
        float sum = 0.0F;
        for (std::size_t k = 0; k < stemCount; ++k) {
            REQUIRE(stems[k].size() == mix.size());
            REQUIRE(std::isfinite(stems[k][i]));
            sum += stems[k][i];
        }
        sumError += static_cast<double>(sum - mix[i]) * (sum - mix[i]);
        mixEnergy += static_cast<double>(mix[i]) * mix[i];
    }
    CHECK(sumError < 0.5 * mixEnergy); // the stems reconstruct most of the mix
}
