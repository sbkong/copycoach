// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

// One ONNX Runtime session over the C API (copy-spec.md section 16.2 B-5): a float tensor in, float tensors
// out, CPU provider. Shared by the prediction engines. Without ONNX Runtime in the build every call reports
// Unsupported.
namespace cc::predict
{

class OrtSession
{
public:
    OrtSession();
    ~OrtSession();
    OrtSession(const OrtSession &) = delete;
    OrtSession &operator=(const OrtSession &) = delete;
    OrtSession(OrtSession &&) = delete;
    OrtSession &operator=(OrtSession &&) = delete;

    /// ModelCorrupt when the file does not load, Unsupported without the runtime.
    [[nodiscard]] Result<void> open(const std::filesystem::path &model);
    [[nodiscard]] bool isOpen() const;

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
