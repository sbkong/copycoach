// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"
#include "core/jobs/job.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <stop_token>
#include <vector>

// File video through FFmpeg (video.md section 5): what is in the file, its audio pulled out into the project's
// cache as FLAC so the rest of the app never sees the video, and frames by time for the video panel. The
// audio clock leads; frames follow (section 9). CPU decoding only (section 8). Not for the audio callback.
namespace cc::media
{

struct VideoInfo {
    int width = 0;
    int height = 0;
    double fps = 0.0;
    double seconds = 0.0;
    int audioStreams = 0;
};

/// FileNotFound, FileCorrupt (not a media file) or Unsupported (no video stream, or a codec FFmpeg lacks).
[[nodiscard]] Result<VideoInfo> probeVideo(const std::filesystem::path &path);

/// The `audioTrack`-th audio stream decoded, mixed down to at most two channels at its own rate, written as
/// FLAC 16 (the stem cache format). Progress by stream position; Cancelled between packets.
[[nodiscard]] Result<void> extractAudio(const std::filesystem::path &video,
                                        int audioTrack,
                                        const std::filesystem::path &flac,
                                        const std::stop_token &stop,
                                        const jobs::Report &report);

/// One decoded frame as packed RGB (3 bytes a pixel, rows top down, no padding).
struct VideoFrame {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb;
    double seconds = 0.0; // the frame's own time
};

class VideoReader
{
public:
    VideoReader();
    ~VideoReader();
    VideoReader(const VideoReader &) = delete;
    VideoReader &operator=(const VideoReader &) = delete;
    VideoReader(VideoReader &&) = delete;
    VideoReader &operator=(VideoReader &&) = delete;

    [[nodiscard]] Result<void> open(const std::filesystem::path &path);
    void close();
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] const VideoInfo &info() const;

    /// The last frame at or before `seconds`. Forward within a second decodes on; anything else seeks to the
    /// keyframe before and decodes up. The first frame when `seconds` is before it.
    [[nodiscard]] Result<VideoFrame> frameAt(double seconds);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace cc::media
