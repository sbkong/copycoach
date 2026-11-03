// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/media/audiofile.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define ENABLE_SNDFILE_WINDOWS_PROTOTYPES 1 // NOLINT(cppcoreguidelines-macro-usage) libsndfile config switch
#endif
#include <sndfile.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <string_view>

namespace cc::media
{

namespace
{

std::string subjectOf(const std::filesystem::path &path)
{
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}

SNDFILE *openNative(const std::filesystem::path &path, int mode, SF_INFO &info)
{
#ifdef _WIN32
    return sf_wchar_open(path.c_str(), mode, &info);
#else
    return sf_open(path.c_str(), mode, &info);
#endif
}

std::string_view formatName(int format)
{
    switch (static_cast<int>(static_cast<unsigned>(format) & static_cast<unsigned>(SF_FORMAT_TYPEMASK))) {
    case SF_FORMAT_WAV:
    case SF_FORMAT_WAVEX:
        return "WAV";
    case SF_FORMAT_FLAC:
        return "FLAC";
    case SF_FORMAT_OGG:
        return "OGG";
    case SF_FORMAT_MPEG:
        return "MP3";
    default:
        return "other";
    }
}

int nativeFormat(AudioFileFormat format)
{
    switch (format) {
    case AudioFileFormat::Flac16:
        return static_cast<int>(static_cast<unsigned>(SF_FORMAT_FLAC) | static_cast<unsigned>(SF_FORMAT_PCM_16));
    case AudioFileFormat::WavFloat:
        return static_cast<int>(static_cast<unsigned>(SF_FORMAT_WAV) | static_cast<unsigned>(SF_FORMAT_FLOAT));
    }
    return 0;
}

} // namespace

AudioFileReader::~AudioFileReader()
{
    close();
}

Result<void> AudioFileReader::open(const std::filesystem::path &path)
{
    close();
    m_path = subjectOf(path);
    if (!std::filesystem::exists(path)) {
        return fail(ErrorCode::FileNotFound, m_path);
    }
    SF_INFO info{};
    m_file = openNative(path, SFM_READ, info);
    if (m_file == nullptr) {
        return fail(ErrorCode::FileCorrupt, m_path, sf_strerror(nullptr));
    }
    m_info = AudioFileInfo{static_cast<double>(info.samplerate),
                           info.channels,
                           info.frames,
                           std::string(formatName(info.format))};
    return {};
}

void AudioFileReader::close()
{
    if (m_file != nullptr) {
        sf_close(m_file);
        m_file = nullptr;
    }
    m_info = {};
}

bool AudioFileReader::isOpen() const
{
    return m_file != nullptr;
}

const AudioFileInfo &AudioFileReader::info() const
{
    return m_info;
}

std::int64_t AudioFileReader::read(std::span<float> interleaved)
{
    if (m_file == nullptr || m_info.channels == 0) {
        return 0;
    }
    assert(interleaved.size() % static_cast<std::size_t>(m_info.channels) == 0);
    const auto frames = static_cast<sf_count_t>(interleaved.size() / static_cast<std::size_t>(m_info.channels));
    return sf_readf_float(m_file, interleaved.data(), frames);
}

std::int64_t AudioFileReader::readPlanar(std::span<const std::span<float>> channels)
{
    if (m_file == nullptr || channels.size() != static_cast<std::size_t>(m_info.channels) || channels.empty()) {
        return 0;
    }
    const std::size_t frames = channels.front().size();
    m_scratch.resize(frames * channels.size());
    const auto got = static_cast<std::size_t>(read(m_scratch));
    for (std::size_t c = 0; c < channels.size(); ++c) {
        for (std::size_t f = 0; f < got; ++f) {
            channels[c][f] = m_scratch[(f * channels.size()) + c];
        }
        std::ranges::fill(channels[c].subspan(got), 0.0F);
    }
    return static_cast<std::int64_t>(got);
}

Result<void> AudioFileReader::seek(std::int64_t frame)
{
    if (m_file == nullptr) {
        return fail(ErrorCode::FileCorrupt, m_path, "not open");
    }
    if (sf_seek(m_file, frame, SEEK_SET) < 0) {
        return fail(ErrorCode::FileCorrupt, m_path, sf_strerror(m_file));
    }
    return {};
}

AudioFileWriter::~AudioFileWriter()
{
    if (m_file != nullptr) {
        sf_close(m_file);
    }
}

Result<void>
AudioFileWriter::open(const std::filesystem::path &path, double sampleRate, int channels, AudioFileFormat format)
{
    if (m_file != nullptr) {
        sf_close(m_file);
        m_file = nullptr;
    }
    m_path = subjectOf(path);
    m_channels = channels;
    SF_INFO info{};
    info.samplerate = static_cast<int>(sampleRate);
    info.channels = channels;
    info.format = nativeFormat(format);
    m_file = openNative(path, SFM_WRITE, info);
    if (m_file == nullptr) {
        return fail(ErrorCode::WriteFailed, m_path, sf_strerror(nullptr));
    }
    return {};
}

Result<void> AudioFileWriter::write(std::span<const float> interleaved)
{
    if (m_file == nullptr) {
        return fail(ErrorCode::WriteFailed, m_path, "not open");
    }
    const auto frames = static_cast<sf_count_t>(interleaved.size() / static_cast<std::size_t>(m_channels));
    if (sf_writef_float(m_file, interleaved.data(), frames) != frames) {
        return fail(ErrorCode::WriteFailed, m_path, sf_strerror(m_file));
    }
    return {};
}

Result<void> AudioFileWriter::close()
{
    if (m_file == nullptr) {
        return {};
    }
    const int status = sf_close(m_file);
    m_file = nullptr;
    if (status != 0) {
        return fail(ErrorCode::WriteFailed, m_path, sf_error_number(status));
    }
    return {};
}

bool AudioFileWriter::isOpen() const
{
    return m_file != nullptr;
}

} // namespace cc::media
