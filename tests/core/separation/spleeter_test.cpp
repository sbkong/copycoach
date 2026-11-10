// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Spleeter separation (copy-spec.md section 4 "스템"): the whole host recipe (STFT, masks from the exported
// graph, zero extension above 16 kHz, inverse STFT, streaming splits) reproduces the Python reference stems
// of the export fixture. Skips without the exported model.
#include "core/separation/spleeter.h"
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

const std::filesystem::path s_model = std::filesystem::path(CC_SPLEETER_DIR) / spleeterModel.name;
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

TEST_CASE("spleeter: the fixture mix separates into the Python reference stems")
{
    if (!std::filesystem::exists(s_model)) {
        SKIP("spleeter-4stems.onnx not exported (tools/models/export_spleeter.py)");
    }
    const std::vector<float> mix = readFloats(s_fixtureDir / "spleeter-mix.f32");
    const std::vector<float> reference = readFloats(s_fixtureDir / "spleeter-stems.f32");
    REQUIRE(reference.size() == mix.size() * stemCount);
    std::array<std::vector<float>, stemCount> stems;
    const StemSink sink = [&stems](std::size_t stem, std::span<const float> interleaved) -> Result<void> {
        stems[stem].insert(stems[stem].end(), interleaved.begin(), interleaved.end());
        return {};
    };
    std::vector<double> progress;
    const std::stop_token never;
    const Result<void> r = separateSpleeter(mix, s_model, sink, never, [&progress](double p) {
        progress.push_back(p);
    });
    REQUIRE(r);
    REQUIRE(progress.back() == 1.0);
    float worst = 0.0F;
    for (std::size_t k = 0; k < stemCount; ++k) {
        REQUIRE(stems[k].size() == mix.size());
        for (std::size_t i = 0; i < mix.size(); ++i) {
            worst = std::max(worst, std::abs(stems[k][i] - reference[(k * mix.size()) + i]));
        }
    }
    CHECK(worst < 2e-3F); // float FFTs against the float64 numpy reference
    double bass = 0.0;
    double vocals = 0.0;
    for (std::size_t i = 0; i < mix.size(); ++i) {
        bass += static_cast<double>(stems[2][i]) * stems[2][i];
        vocals += static_cast<double>(stems[0][i]) * stems[0][i];
    }
    CHECK(bass > 100.0 * vocals); // the fixture is a bass line and clicks: no voice
}
