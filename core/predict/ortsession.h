// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// One ONNX Runtime session over the C API (copy-spec.md section 16.2 B-5, spikes.md (a)): a float tensor in,
// float tensors out. The graph runs on the WebGPU plugin execution provider (D3D12/Vulkan through Dawn, any
// GPU vendor; `onnxruntime_providers_webgpu.dll` next to the executable, registered once per process) when
// the accelerator setting allows and a device answers, otherwise on the CPU; nodes the plugin lacks fall
// back to the CPU inside the runtime. Shared by every prediction and separation engine. Without ONNX Runtime
// in the build every call reports Unsupported.
namespace cc::predict
{

/// The preferences value `separation.accelerator`: auto = WebGPU when it works, otherwise the CPU; the CUDA
/// pack does not exist yet and counts as auto.
enum class Accelerator : std::uint8_t {
    Auto,
    WebGpu, // NoAccelerator when no device answers (the user asked for it explicitly)
    Cpu,
};

enum class Provider : std::uint8_t {
    Cpu,
    WebGpu,
};

/// Process-wide default the shell sets from the preferences; engines on the job thread read it when they
/// open a session (a change applies from the next job on).
void setDefaultAccelerator(Accelerator accelerator);
[[nodiscard]] Accelerator defaultAccelerator();
/// The provider of the session opened most recently (status text after a job).
[[nodiscard]] Provider lastProvider();
[[nodiscard]] std::string_view providerName(Provider provider); // "WebGPU" / "CPU"

class OrtSession
{
public:
    OrtSession();
    ~OrtSession();
    OrtSession(const OrtSession &) = delete;
    OrtSession &operator=(const OrtSession &) = delete;
    OrtSession(OrtSession &&) = delete;
    OrtSession &operator=(OrtSession &&) = delete;

    /// ModelCorrupt when the file does not load, Unsupported without the runtime, NoAccelerator when WebGPU
    /// was demanded and is unavailable. `accelerator` overrides the process default.
    [[nodiscard]] Result<void> open(const std::filesystem::path &model,
                                    std::optional<Accelerator> accelerator = std::nullopt);
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] Provider provider() const;

    /// Runs the graph with `input` of `shape` and fills one vector per requested output, in order.
    [[nodiscard]] Result<void> run(const char *inputName,
                                   std::span<const std::int64_t> shape,
                                   std::span<const float> input,
                                   std::span<const char *const> outputNames,
                                   std::vector<std::vector<float>> &outputs) const;

private:
    struct Impl;
    Impl *m_impl;
};

} // namespace cc::predict
