// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

struct sf_private_tag;

// Streaming audio file access through libsndfile (audio-engine.md section 15.1): WAV, FLAC, OGG/Opus and MP3 in,
// FLAC or float WAV out. Music files are read in chunks on the render worker, never loaded whole; the small
// bundled sound sets keep using io::readWavFile. Samples are full-scale floats (coding-standards.md section 18).
namespace cc::media
{

struct AudioFileInfo {
    double sampleRate = 0.0;
    int channels = 0;
    std::int64_t frames = 0;
    std::string format; // "WAV", "FLAC", "OGG", "MP3" or "other"
};

class AudioFileReader
{
public:
    AudioFileReader() = default;
    ~AudioFileReader();
    AudioFileReader(const AudioFileReader &) = delete;
    AudioFileReader &operator=(const AudioFileReader &) = delete;
    AudioFileReader(AudioFileReader &&) = delete;
    AudioFileReader &operator=(AudioFileReader &&) = delete;

    /// FileNotFound when the path does not exist, FileCorrupt when libsndfile cannot read it.
    [[nodiscard]] Result<void> open(const std::filesystem::path &path);
    void close();
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] const AudioFileInfo &info() const;

    /// Reads interleaved frames; `interleaved.size()` must be a multiple of the channel count. Returns the
    /// frames actually read, fewer than asked only at the end of the file.
    std::int64_t read(std::span<float> interleaved);
    /// Reads into one span per channel (all the same length) and de-interleaves. Returns the frames read.
    std::int64_t readPlanar(std::span<const std::span<float>> channels);
    [[nodiscard]] Result<void> seek(std::int64_t frame);

private:
    sf_private_tag *m_file = nullptr;
    AudioFileInfo m_info;
    std::string m_path; // the error subject
    std::vector<float> m_scratch; // interleaved staging for readPlanar
};

enum class AudioFileFormat : std::uint8_t {
    Flac16, // stem cache (copy-spec.md section 16.2 D2)
    WavFloat,
};

class AudioFileWriter
{
public:
    AudioFileWriter() = default;
    ~AudioFileWriter();
    AudioFileWriter(const AudioFileWriter &) = delete;
    AudioFileWriter &operator=(const AudioFileWriter &) = delete;
    AudioFileWriter(AudioFileWriter &&) = delete;
    AudioFileWriter &operator=(AudioFileWriter &&) = delete;

    [[nodiscard]] Result<void>
    open(const std::filesystem::path &path, double sampleRate, int channels, AudioFileFormat format);
    /// Interleaved frames; WriteFailed when the file system refuses.
    [[nodiscard]] Result<void> write(std::span<const float> interleaved);
    [[nodiscard]] Result<void> close();
    [[nodiscard]] bool isOpen() const;

private:
    sf_private_tag *m_file = nullptr;
    int m_channels = 0;
    std::string m_path;
};

} // namespace cc::media
