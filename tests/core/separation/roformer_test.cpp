// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// BS-RoFormer host side (copy-spec.md section 4 "스템 모델 재조사 2026-11-17"): the torch-style STFT pair
// reconstructs a chunk, demix's chunk weights fade only inside the song, and, when the user's converted
// graph sits in the local research folder, the fixture mix comes out as the six stems the Python reference
// (export_bs_roformer.py) wrote next to it.
#include "core/predict/ortsession.h"
#include "core/separation/roformer.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <span>
#include <string>
#include <vector>

using namespace cc;
using namespace cc::separation;

namespace
{

const std::filesystem::path s_dir = std::filesystem::path(CC_BS_ROFORMER_DIR);
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

TEST_CASE("roformer: the STFT pair reconstructs a chunk")
{
    std::vector<float> chunk(roformerChunk);
    for (std::size_t i = 0; i < chunk.size(); ++i) {
        const double t = static_cast<double>(i) / 44100.0;
        chunk[i] = static_cast<float>((0.5 * std::sin(2.0 * std::numbers::pi * 220.0 * t))
                                      + (0.2 * std::sin(2.0 * std::numbers::pi * 3001.0 * t)));
    }
    const RoformerStft stft;
    std::vector<std::complex<float>> spectrum(roformerFrames * roformerBins);
    stft.analyze(chunk, spectrum);
    std::vector<float> back(roformerChunk);
    stft.synthesize(spectrum, back);
    float worst = 0.0F;
    for (std::size_t i = 0; i < chunk.size(); ++i) {
        worst = std::max(worst, std::abs(back[i] - chunk[i]));
    }
    CHECK(worst < 1e-4F);
    CHECK(roformerFrames == 1151);
}

TEST_CASE("roformer: demix's chunk weights fade linearly, except at the song's ends")
{
    const std::vector<float> middle = roformerWeights(false, false);
    CHECK(middle[0] == 0.0F);
    CHECK(middle[roformerFade - 1] == 1.0F);
    CHECK(middle[roformerChunk / 2] == 1.0F);
    CHECK(middle[roformerChunk - 1] == 0.0F);
    const std::vector<float> first = roformerWeights(true, false);
    CHECK(first[0] == 1.0F);
    CHECK(first[roformerChunk - 1] == 0.0F);
    const std::vector<float> last = roformerWeights(false, true);
    CHECK(last[0] == 0.0F);
    CHECK(last[roformerChunk - 1] == 1.0F);
}

namespace
{

/// One separation of the fixture mix under the given accelerator: stems in stemNames order, interleaved.
std::array<std::vector<float>, maxStemCount> separateFixture(const std::vector<float> &mix,
                                                             predict::Accelerator accelerator)
{
    const predict::Accelerator before = predict::defaultAccelerator();
    predict::setDefaultAccelerator(accelerator);
    std::array<std::vector<float>, maxStemCount> stems;
    const StemSink sink = [&stems](std::size_t stem, std::span<const float> interleaved) -> Result<void> {
        stems[stem].insert(stems[stem].end(), interleaved.begin(), interleaved.end());
        return {};
    };
    const std::stop_token never;
    const Result<void> r = separateRoformer(mix, s_dir / roformerFileName, sink, never, [](double) { });
    predict::setDefaultAccelerator(before);
    INFO((r ? std::string("ok") : r.error().subject + ": " + r.error().detail));
    REQUIRE(r);
    return stems;
}

float maxDifference(const std::vector<float> &a, const std::vector<float> &b)
{
    REQUIRE(a.size() == b.size());
    float worst = 0.0F;
    for (std::size_t i = 0; i < a.size(); ++i) {
        worst = std::max(worst, std::abs(a[i] - b[i]));
    }
    return worst;
}

} // namespace

TEST_CASE("roformer: a local research model separates the fixture mix like the Python reference")
{
    if (!std::filesystem::exists(s_dir / roformerFileName)) {
        SKIP("bs_roformer.onnx not present (provenance unknown, never downloaded by the app)");
    }
    const std::vector<float> mix = readFloats(s_fixtureDir / "spleeter-mix.f32");
    const std::vector<float> reference = readFloats(s_dir / "reference-stems.f32"); // [model stem][channel][n]
    const std::size_t n = mix.size() / stemChannels;
    REQUIRE(reference.size() == maxStemCount * stemChannels * n);
    const auto cpu = separateFixture(mix, predict::Accelerator::Cpu);
    constexpr std::array<std::size_t, maxStemCount> modelOf{3,
                                                            1,
                                                            0,
                                                            2,
                                                            4,
                                                            5}; // stemNames -> bass drums other vocals guitar piano
    std::vector<float> sum(mix.size(), 0.0F);
    float worst = 0.0F;
    for (std::size_t k = 0; k < maxStemCount; ++k) {
        REQUIRE(cpu[k].size() == mix.size());
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t c = 0; c < stemChannels; ++c) {
                const float expected = reference[(((modelOf[k] * stemChannels) + c) * n) + i];
                worst = std::max(worst, std::abs(cpu[k][(i * stemChannels) + c] - expected));
                sum[(i * stemChannels) + c] += cpu[k][(i * stemChannels) + c];
            }
        }
    }
    INFO("CPU: max difference to the Python reference " << worst << ", stems sum vs mix " << maxDifference(sum, mix));
    CHECK(worst < 2e-3F);
    const auto gpu = separateFixture(mix, predict::Accelerator::Auto); // the WebGPU path, when a device answers
    if (predict::lastProvider() == predict::Provider::Cpu) {
        return;
    }
    float gpuWorst = 0.0F;
    float gpuPeak = 0.0F;
    for (std::size_t k = 0; k < maxStemCount; ++k) {
        gpuWorst = std::max(gpuWorst, maxDifference(gpu[k], cpu[k]));
        gpuPeak = std::max(gpuPeak, maxDifference(gpu[k], std::vector<float>(gpu[k].size(), 0.0F)));
    }
    INFO("WebGPU vs CPU max difference " << gpuWorst << ", WebGPU peak " << gpuPeak);
    CHECK(gpuWorst < 1e-2F);
}
