// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/io/wavfile.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <span>
#include <string_view>

namespace cc::io
{

namespace
{

using Bytes = std::span<const std::byte>;

constexpr std::uint16_t formatPcm = 1;
constexpr std::uint16_t formatFloat = 3;
constexpr std::uint16_t formatExtensible = 0xFFFE;
constexpr std::size_t riffHeaderBytes = 12;
constexpr std::size_t chunkHeaderBytes = 8;

struct Format {
    std::uint16_t tag = 0;
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bitsPerSample = 0;
};

template<typename T>
T readLittle(Bytes bytes, std::size_t offset)
{
    T value{};
    std::memcpy(&value, bytes.subspan(offset, sizeof(T)).data(), sizeof(T));
    if constexpr (std::endian::native == std::endian::big && std::is_integral_v<T>) {
        value = std::byteswap(value);
    }
    return value;
}

constexpr std::uint32_t fourcc(std::string_view id)
{
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(id[i])) << (8U * i);
    }
    return value;
}

Result<std::vector<char>> readAll(const std::filesystem::path &path)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return fail(ErrorCode::FileNotFound, path.string());
    }
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec || size > maxWavFileBytes) {
        return fail(ErrorCode::FileCorrupt, path.string(), "file larger than the WAV limit");
    }
    std::ifstream in(path, std::ios::binary);
    std::vector<char> bytes(static_cast<std::size_t>(size));
    if (!in.read(bytes.data(), static_cast<std::streamsize>(size))) {
        return fail(ErrorCode::FileCorrupt, path.string(), "cannot read");
    }
    return bytes;
}

float decodeSample(Bytes bytes, std::size_t offset, const Format &format)
{
    if (format.tag == formatFloat) {
        return readLittle<float>(bytes, offset);
    }
    if (format.bitsPerSample == 16) {
        return static_cast<float>(readLittle<std::int16_t>(bytes, offset)) / 32768.0F;
    }
    const std::uint32_t raw = std::to_integer<std::uint32_t>(bytes[offset])
        | (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U)
        | (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U);
    const auto value = static_cast<std::int32_t>(raw >= (1U << 23U) ? raw - (1U << 24U) : raw);
    return static_cast<float>(value) / 8388608.0F;
}

bool formatSupported(const Format &format)
{
    if (format.channels == 0 || format.sampleRate == 0) {
        return false;
    }
    if (format.tag == formatFloat) {
        return format.bitsPerSample == 32;
    }
    return format.tag == formatPcm && (format.bitsPerSample == 16 || format.bitsPerSample == 24);
}

Format parseFormat(Bytes chunk)
{
    Format format{readLittle<std::uint16_t>(chunk, 0),
                  readLittle<std::uint16_t>(chunk, 2),
                  readLittle<std::uint32_t>(chunk, 4),
                  readLittle<std::uint16_t>(chunk, 14)};
    if (format.tag == formatExtensible && chunk.size() >= 26) {
        format.tag = readLittle<std::uint16_t>(chunk, 24); // first two bytes of the sub-format GUID
    }
    return format;
}

AudioClip decode(Bytes data, const Format &format)
{
    AudioClip clip;
    clip.sampleRate = format.sampleRate;
    const std::size_t bytesPerSample = format.bitsPerSample / 8U;
    const std::size_t frames = data.size() / (bytesPerSample * format.channels);
    clip.channels.assign(format.channels, std::vector<float>(frames));
    std::size_t offset = 0;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (std::size_t ch = 0; ch < format.channels; ++ch) {
            clip.channels[ch][frame] = decodeSample(data, offset, format);
            offset += bytesPerSample;
        }
    }
    return clip;
}

bool isRiffWave(Bytes all)
{
    return all.size() >= riffHeaderBytes && readLittle<std::uint32_t>(all, 0) == fourcc("RIFF")
        && readLittle<std::uint32_t>(all, 8) == fourcc("WAVE");
}

Result<AudioClip> decodeData(Bytes body, const Format &format, const std::filesystem::path &path)
{
    if (!formatSupported(format)) {
        return fail(ErrorCode::FileCorrupt, path.string(), "unsupported WAV format (need PCM 16/24 or float 32)");
    }
    return decode(body, format);
}

/// Walks the chunks: remembers the format, decodes the data chunk when it is reached.
Result<AudioClip> decodeChunks(Bytes all, const std::filesystem::path &path)
{
    Format format;
    std::size_t offset = riffHeaderBytes;
    while (offset + chunkHeaderBytes <= all.size()) {
        const auto id = readLittle<std::uint32_t>(all, offset);
        const auto size = static_cast<std::size_t>(readLittle<std::uint32_t>(all, offset + 4));
        const std::size_t body = offset + chunkHeaderBytes;
        if (body + size > all.size()) {
            break;
        }
        if (id == fourcc("fmt ") && size >= 16) {
            format = parseFormat(all.subspan(body, size));
        } else if (id == fourcc("data")) {
            return decodeData(all.subspan(body, size), format, path);
        }
        offset = body + size + (size % 2); // chunks are word aligned
    }
    return fail(ErrorCode::FileCorrupt, path.string(), "no data chunk");
}

} // namespace

Result<AudioClip> readWavFile(const std::filesystem::path &path)
{
    auto bytes = readAll(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    const Bytes all = std::as_bytes(std::span<const char>(*bytes));
    if (!isRiffWave(all)) {
        return fail(ErrorCode::FileCorrupt, path.string(), "not a RIFF/WAVE file");
    }
    return decodeChunks(all, path);
}

std::vector<float> toMono(const AudioClip &clip)
{
    std::vector<float> mono(clip.frames(), 0.0F);
    if (clip.channels.empty()) {
        return mono;
    }
    const float scale = 1.0F / static_cast<float>(clip.channels.size());
    for (const std::vector<float> &channel : clip.channels) {
        for (std::size_t i = 0; i < mono.size(); ++i) {
            mono[i] += channel[i] * scale;
        }
    }
    return mono;
}

std::vector<float> resampleLinear(const std::vector<float> &samples, double sourceRate, double targetRate)
{
    if (sourceRate == targetRate || samples.empty()) {
        return samples;
    }
    const double ratio = sourceRate / targetRate;
    const auto frames = static_cast<std::size_t>(std::floor(static_cast<double>(samples.size() - 1) / ratio)) + 1;
    std::vector<float> out(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const double position = static_cast<double>(i) * ratio;
        const auto index = static_cast<std::size_t>(position);
        const auto fraction = static_cast<float>(position - static_cast<double>(index));
        const float next = index + 1 < samples.size() ? samples[index + 1] : samples[index];
        out[i] = samples[index] + ((next - samples[index]) * fraction);
    }
    return out;
}

} // namespace cc::io
