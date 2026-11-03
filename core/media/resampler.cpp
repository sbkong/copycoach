// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/media/resampler.h"

#include <samplerate.h>

#include <cstddef>

namespace cc::media
{

Resampler::Resampler(int channels)
    : m_channels(channels)
{
    int error = 0;
    m_state = src_new(SRC_SINC_MEDIUM_QUALITY, channels, &error);
}

Resampler::~Resampler()
{
    if (m_state != nullptr) {
        src_delete(m_state);
    }
}

Result<Resampler::Progress>
Resampler::process(std::span<const float> input, std::span<float> output, double ratio, bool endOfInput)
{
    if (m_state == nullptr) {
        return fail(ErrorCode::Unsupported, "resampler", "libsamplerate state could not be created");
    }
    const auto perFrame = static_cast<std::size_t>(m_channels);
    SRC_DATA data{};
    data.data_in = input.data();
    data.input_frames = static_cast<long>(input.size() / perFrame);
    data.data_out = output.data();
    data.output_frames = static_cast<long>(output.size() / perFrame);
    data.src_ratio = ratio;
    data.end_of_input = endOfInput ? 1 : 0;
    const int error = src_process(m_state, &data);
    if (error != 0) {
        return fail(ErrorCode::Unsupported, "resampler", src_strerror(error));
    }
    return Progress{data.input_frames_used, data.output_frames_gen};
}

void Resampler::reset()
{
    if (m_state != nullptr) {
        src_reset(m_state);
    }
}

int Resampler::channels() const
{
    return m_channels;
}

} // namespace cc::media
