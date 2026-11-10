// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/predict/ortsession.h"

#include <array>
#include <atomic>
#include <mutex>
#include <numeric>

#if CC_HAVE_ORT
#include <onnxruntime_c_api.h>
#endif

namespace cc::predict
{

namespace
{

std::atomic<Accelerator> &defaultAcceleratorSlot()
{
    static std::atomic<Accelerator> slot{Accelerator::Auto};
    return slot;
}

std::atomic<Provider> &lastProviderSlot()
{
    static std::atomic<Provider> slot{Provider::Cpu};
    return slot;
}

} // namespace

void setDefaultAccelerator(Accelerator accelerator)
{
    defaultAcceleratorSlot().store(accelerator);
}

Accelerator defaultAccelerator()
{
    return defaultAcceleratorSlot().load();
}

Provider lastProvider()
{
    return lastProviderSlot().load();
}

std::string_view providerName(Provider provider)
{
    return provider == Provider::WebGpu ? "WebGPU" : "CPU";
}

#if CC_HAVE_ORT

namespace
{

constexpr const char *webGpuEpName = "WebGpuExecutionProvider"; // what the plugin reports and registers as
constexpr const ORTCHAR_T *webGpuLibrary = L"onnxruntime_providers_webgpu.dll"; // next to the executable

/// The one OrtEnv of the process: plugin libraries register with an env, once; sessions share it.
class SharedEnv
{
public:
    static SharedEnv &instance()
    {
        static SharedEnv env;
        return env;
    }

    [[nodiscard]] OrtEnv *env() const
    {
        return m_env;
    }

    [[nodiscard]] const OrtApi *api() const
    {
        return m_api;
    }

    /// Registers the WebGPU plugin on first use; false when the library is missing or refuses.
    [[nodiscard]] bool ensureWebGpu()
    {
        const std::scoped_lock lock(m_mutex);
        if (m_webGpuTried) {
            return m_webGpuRegistered;
        }
        m_webGpuTried = true;
        if (m_env == nullptr) {
            return false;
        }
        OrtStatus *status = m_api->RegisterExecutionProviderLibrary(m_env, webGpuEpName, webGpuLibrary);
        if (status != nullptr) {
            m_api->ReleaseStatus(status);
            return false;
        }
        m_webGpuRegistered = true;
        return true;
    }

private:
    SharedEnv()
        : m_api(OrtGetApiBase()->GetApi(ORT_API_VERSION))
    {
        if (m_api != nullptr) {
            if (OrtStatus *status = m_api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "copycoach", &m_env); status != nullptr) {
                m_api->ReleaseStatus(status);
                m_env = nullptr;
            }
        }
    }

    const OrtApi *m_api;
    OrtEnv *m_env = nullptr;
    std::mutex m_mutex;
    bool m_webGpuTried = false;
    bool m_webGpuRegistered = false;
};

} // namespace

struct OrtSession::Impl {
    Impl() = default;
    Impl(const Impl &) = delete;
    Impl &operator=(const Impl &) = delete;
    Impl(Impl &&) = delete;
    Impl &operator=(Impl &&) = delete;

    const OrtApi *api = nullptr;
    OrtEnv *env = nullptr; // owned by SharedEnv
    OrtSessionOptions *options = nullptr;
    ::OrtSession *session = nullptr;
    OrtMemoryInfo *memory = nullptr;
    std::string subject;
    Provider provider = Provider::Cpu;

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
    }

    [[nodiscard]] Error errorOf(OrtStatus *status) const
    {
        const std::string detail = api->GetErrorMessage(status);
        api->ReleaseStatus(status);
        return Error{ErrorCode::ModelCorrupt, subject, detail};
    }

    /// The first WebGPU device the plugin lists (the plugin takes one device per session), appended to the
    /// options; false when there is none or the runtime refuses.
    [[nodiscard]] bool appendWebGpu() const
    {
        const OrtEpDevice *const *devices = nullptr;
        std::size_t count = 0;
        if (OrtStatus *status = api->GetEpDevices(env, &devices, &count); status != nullptr) {
            api->ReleaseStatus(status);
            return false;
        }
        for (std::size_t i = 0; i < count; ++i) {
            const OrtEpDevice *device = devices[i]; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) C API
            if (std::string_view(api->EpDevice_EpName(device)) != webGpuEpName) {
                continue;
            }
            OrtStatus *status =
                api->SessionOptionsAppendExecutionProvider_V2(options, env, &device, 1, nullptr, nullptr, 0);
            if (status != nullptr) {
                api->ReleaseStatus(status);
                return false;
            }
            return true;
        }
        return false;
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

/// Auto tries WebGPU and settles for the CPU; WebGpu insists; Cpu never asks.
Result<void> OrtSession::open(const std::filesystem::path &model, std::optional<Accelerator> accelerator)
{
    Impl &h = *m_impl;
    SharedEnv &shared = SharedEnv::instance();
    h.subject = model.string();
    h.api = shared.api();
    h.env = shared.env();
    if (h.api == nullptr || h.env == nullptr) {
        return fail(ErrorCode::Unsupported, "onnxruntime", "API version mismatch");
    }
    OrtStatus *status = h.api->CreateSessionOptions(&h.options);
    if (status != nullptr) {
        return std::unexpected(h.errorOf(status));
    }
    const Accelerator wanted = accelerator.value_or(defaultAccelerator());
    if (wanted != Accelerator::Cpu && shared.ensureWebGpu() && h.appendWebGpu()) {
        h.provider = Provider::WebGpu;
    } else if (wanted == Accelerator::WebGpu) {
        return fail(ErrorCode::NoAccelerator, h.subject, "WebGPU execution provider unavailable");
    }
    status = h.api->CreateSession(h.env, model.c_str(), h.options, &h.session);
    if (status == nullptr) {
        status = h.api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &h.memory);
    }
    if (status != nullptr) {
        return std::unexpected(h.errorOf(status));
    }
    lastProviderSlot().store(h.provider);
    return {};
}

bool OrtSession::isOpen() const
{
    return m_impl->session != nullptr;
}

Provider OrtSession::provider() const
{
    return m_impl->provider;
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

Result<void> OrtSession::open(const std::filesystem::path &model, std::optional<Accelerator>)
{
    return fail(ErrorCode::Unsupported, model.string(), "built without ONNX Runtime");
}

bool OrtSession::isOpen() const
{
    return false;
}

Provider OrtSession::provider() const
{
    return Provider::Cpu;
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
