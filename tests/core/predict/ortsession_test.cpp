// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// The shared ONNX Runtime session (copy-spec.md section 16.2 B-5): the CPU path always answers, and when the
// WebGPU plugin next to the test executable finds a device, the same graph gives the same numbers on it.
// Skips the GPU half where no device answers (a headless CI, a session without D3D12).
#include "core/predict/basicpitch.h"
#include "core/predict/ortsession.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <random>
#include <vector>

using namespace cc;
using namespace cc::predict;

namespace
{

const std::filesystem::path s_model = std::filesystem::path(CC_DATA_DIR) / basicPitchModelFile;
constexpr const char *input = "serving_default_input_2:0";
constexpr std::array<const char *, 1> outputs{"StatefulPartitionedCall:1"};

std::vector<float> noiseWindow()
{
    std::mt19937 rng(11); // NOLINT(cert-msc32-c,cert-msc51-cpp,bugprone-random-generator-seed) fixed fixture
    std::uniform_real_distribution<float> dist(-0.3F, 0.3F);
    std::vector<float> window(basicPitchWindowSamples);
    for (float &v : window) {
        v = dist(rng);
    }
    return window;
}

} // namespace

TEST_CASE("ortsession: the CPU provider runs the bundled note model")
{
    REQUIRE(std::filesystem::exists(s_model));
    OrtSession session;
    REQUIRE(session.open(s_model, Accelerator::Cpu));
    REQUIRE(session.provider() == Provider::Cpu);
    REQUIRE(lastProvider() == Provider::Cpu);
    CHECK(providerName(Provider::Cpu) == "CPU");
}

TEST_CASE("ortsession: WebGPU, when a device answers, gives the CPU's numbers")
{
    REQUIRE(std::filesystem::exists(s_model));
    OrtSession gpu;
    const Result<void> opened = gpu.open(s_model, Accelerator::WebGpu);
    if (!opened) {
        REQUIRE(opened.error().code == ErrorCode::NoAccelerator);
        SKIP("no WebGPU device (plugin missing or no D3D12/Vulkan adapter in this session)");
    }
    REQUIRE(gpu.provider() == Provider::WebGpu);
    REQUIRE(lastProvider() == Provider::WebGpu);
    OrtSession cpu;
    REQUIRE(cpu.open(s_model, Accelerator::Cpu));
    const std::vector<float> window = noiseWindow();
    const std::array<std::int64_t, 3> shape{1, static_cast<std::int64_t>(basicPitchWindowSamples), 1};
    std::vector<std::vector<float>> fromGpu;
    std::vector<std::vector<float>> fromCpu;
    REQUIRE(gpu.run(input, shape, window, outputs, fromGpu));
    REQUIRE(cpu.run(input, shape, window, outputs, fromCpu));
    REQUIRE(fromGpu[0].size() == fromCpu[0].size());
    float worst = 0.0F;
    for (std::size_t i = 0; i < fromCpu[0].size(); ++i) {
        worst = std::max(worst, std::abs(fromGpu[0][i] - fromCpu[0][i]));
    }
    CHECK(worst < 1e-3F);

    // auto settles for whatever answers; the default is read when a session opens
    setDefaultAccelerator(Accelerator::Auto);
    OrtSession automatic;
    REQUIRE(automatic.open(s_model));
    CHECK(automatic.provider() == Provider::WebGpu);
    setDefaultAccelerator(Accelerator::Cpu);
    OrtSession plain;
    REQUIRE(plain.open(s_model));
    CHECK(plain.provider() == Provider::Cpu);
    setDefaultAccelerator(Accelerator::Auto);
}
