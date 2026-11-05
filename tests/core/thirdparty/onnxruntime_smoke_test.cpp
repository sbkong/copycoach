// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// The ONNX Runtime download (cmake/OnnxRuntime.cmake): the DLL next to the executable is the pinned version and
// its C API answers. Execution providers beyond the CPU are checked when the stems job arrives.
#include <catch2/catch_test_macros.hpp>

#include <onnxruntime_c_api.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

TEST_CASE("onnxruntime: the pinned version answers through the C API and offers the CPU provider")
{
    const OrtApiBase *base = OrtGetApiBase();
    REQUIRE(base != nullptr);
    REQUIRE(std::string(base->GetVersionString()) == "1.31.0");
    const OrtApi *api = base->GetApi(ORT_API_VERSION);
    REQUIRE(api != nullptr);

    char **providers = nullptr;
    int count = 0;
    REQUIRE(api->GetAvailableProviders(&providers, &count) == nullptr);
    std::vector<std::string> names;
    names.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        names.emplace_back(providers[i]); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) C API
    }
    REQUIRE(api->ReleaseAvailableProviders(providers, count) == nullptr);
    REQUIRE(std::ranges::find(names, "CPUExecutionProvider") != names.end());
}
