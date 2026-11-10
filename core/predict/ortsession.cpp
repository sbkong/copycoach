// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/predict/ortsession.h"

#include <array>
#include <numeric>

#if CC_HAVE_ORT
#include <onnxruntime_c_api.h>
#endif

namespace cc::predict
{

#if CC_HAVE_ORT

struct OrtSession::Impl {
    Impl() = default;
    Impl(const Impl &) = delete;
    Impl &operator=(const Impl &) = delete;
    Impl(Impl &&) = delete;
    Impl &operator=(Impl &&) = delete;

    const OrtApi *api = nullptr;
    OrtEnv *env = nullptr;
    OrtSessionOptions *options = nullptr;
    ::OrtSession *session = nullptr;
    OrtMemoryInfo *memory = nullptr;
    std::string subject;

    ~Impl()
    {
        if (api == nullptr) {
            return;
        }
        if (memory != nullptr) {
            api->ReleaseMemoryInfo(memory);
        }
        if (session != nullptr) {
            api->ReleaseSession(session);
        }
        if (options != nullptr) {
            api->ReleaseSessionOptions(options);
        }
        if (env != nullptr) {
            api->ReleaseEnv(env);
        }
    }

    [[nodiscard]] Error errorOf(OrtStatus *status) const
    {
        const std::string detail = api->GetErrorMessage(status);
        api->ReleaseStatus(status);
        return Error{ErrorCode::ModelCorrupt, subject, detail};
    }

    /// The output tensors copied into plain vectors and released.
    [[nodiscard]] Result<void> collect(std::span<OrtValue *> values, std::vector<std::vector<float>> &outputs) const
    {
        Result<void> result;
        outputs.assign(values.size(), {});
        for (std::size_t i = 0; i < values.size(); ++i) {
            OrtTensorTypeAndShapeInfo *info = nullptr;
            std::size_t count = 0;
            float *data = nullptr;
            OrtStatus *status = api->GetTensorTypeAndShape(values[i], &info);
            if (status == nullptr) {
                status = api->GetTensorShapeElementCount(info, &count);
                api->ReleaseTensorTypeAndShapeInfo(info);
            }
            if (status == nullptr) {
                status = api->GetTensorMutableData(values[i], reinterpret_cast<void **>(&data)); // NOLINT
            }
            if (status != nullptr) {
                result = std::unexpected(errorOf(status));
            } else {
                outputs[i].assign(data, data + count); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) C API
            }
            api->ReleaseValue(values[i]);
        }
        return result;
    }
};

OrtSession::OrtSession()
    : m_impl(new Impl())
{
}

OrtSession::~OrtSession()
{
    delete m_impl;
}

Result<void> OrtSession::open(const std::filesystem::path &model)
{
    Impl &h = *m_impl;
    h.subject = model.string();
    h.api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    if (h.api == nullptr) {
        return fail(ErrorCode::Unsupported, "onnxruntime", "API version mismatch");
    }
    OrtStatus *status = h.api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "copycoach", &h.env);
    if (status == nullptr) {
        status = h.api->CreateSessionOptions(&h.options);
    }
    if (status == nullptr) {
        status = h.api->CreateSession(h.env, model.c_str(), h.options, &h.session);
    }
    if (status == nullptr) {
        status = h.api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &h.memory);
    }
    if (status != nullptr) {
        return std::unexpected(h.errorOf(status));
    }
    return {};
}

bool OrtSession::isOpen() const
{
    return m_impl->session != nullptr;
}

Result<void> OrtSession::run(const char *inputName,
                             std::span<const std::int64_t> shape,
                             std::span<const float> input,
                             std::span<const char *const> outputNames,
                             std::vector<std::vector<float>> &outputs) const
{
    const Impl &h = *m_impl;
    if (!isOpen()) {
        return fail(ErrorCode::Unsupported, h.subject, "session not open");
    }
    OrtValue *tensor = nullptr;
    OrtStatus *status = h.api->CreateTensorWithDataAsOrtValue(
        h.memory,
        const_cast<float *>(input.data()), // NOLINT(cppcoreguidelines-pro-type-const-cast) ORT reads only
        input.size() * sizeof(float),
        shape.data(),
        shape.size(),
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
        &tensor);
    if (status != nullptr) {
        return std::unexpected(h.errorOf(status));
    }
    std::vector<OrtValue *> values(outputNames.size(), nullptr);
    status =
        h.api->Run(h.session, nullptr, &inputName, &tensor, 1, outputNames.data(), outputNames.size(), values.data());
    h.api->ReleaseValue(tensor);
    if (status != nullptr) {
        return std::unexpected(h.errorOf(status));
    }
    return h.collect(values, outputs);
}

#else

struct OrtSession::Impl { };

OrtSession::OrtSession()
    : m_impl(nullptr)
{
}

OrtSession::~OrtSession() = default;

Result<void> OrtSession::open(const std::filesystem::path &model)
{
    return fail(ErrorCode::Unsupported, model.string(), "built without ONNX Runtime");
}

bool OrtSession::isOpen() const
{
    return false;
}

Result<void> OrtSession::run(const char *,
                             std::span<const std::int64_t>,
                             std::span<const float>,
                             std::span<const char *const>,
                             std::vector<std::vector<float>> &) const
{
    return fail(ErrorCode::Unsupported, "onnxruntime", "built without ONNX Runtime");
}

#endif

} // namespace cc::predict
