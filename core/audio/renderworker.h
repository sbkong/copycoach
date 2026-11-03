// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/audio/rt/chunkring.h"
#include "core/audio/stretcher.h"
#include "core/error.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

// The render worker (audio-engine.md section 15.2 / 15.4): reads the recording (original or stems) in chunks,
// resamples to the device rate, mixes the stems and pushes 64-frame chunks tagged with their source frame into
// the ring the callback drains. Loops wrap here, on the source timeline. Not real-time: it allocates, locks a
// mutex for GUI commands and sleeps; it never touches engine state directly.
// "Source frame" = frame index on the recording's timeline at the device rate (the TimeMap's sample unit).
namespace cc::audio
{

class RenderWorker
{
public:
    static constexpr int readFrames = 1024; // file frames decoded per read
    static constexpr double minSpeed = 0.25; // user decision 2026-11-03: 25..150 %
    static constexpr double maxSpeed = 1.5;
    static constexpr double maxPitchSemitones = 12.0;
    static constexpr int maxSourceFramesPerChunk = 128; // 64 output frames at 150 % plus rounding
    static constexpr double defaultFillSeconds = 0.25; // user decision 2026-11-03 (section 15.10)

    RenderWorker(rt::ChunkRing &ring, double deviceRate);
    ~RenderWorker();
    RenderWorker(const RenderWorker &) = delete;
    RenderWorker &operator=(const RenderWorker &) = delete;
    RenderWorker(RenderWorker &&) = delete;
    RenderWorker &operator=(RenderWorker &&) = delete;

    /// Opens every file on the calling thread (so the error is immediate); the worker plays them mixed from
    /// source frame 0. One path = the original, several = stems. Replaces the previous set.
    [[nodiscard]] Result<void> load(const std::vector<std::filesystem::path> &paths);
    void unload();

    // GUI thread: take effect at the next chunk the worker renders.
    void seek(std::int64_t sourceFrame); // flushes what the ring holds (new generation)
    void setLoop(std::int64_t fromFrame, std::int64_t toFrame); // to <= from disables
    void setGain(std::size_t source, float gain);
    void setMute(std::size_t source, bool muted);
    void setSolo(std::size_t source, bool solo);
    void setFillTarget(std::int64_t frames);
    /// Playback speed factor (1.0 = the recording's own tempo) and transposition; 1.0 / 0 bypass the stretcher.
    void setSpeed(double factor);
    void setPitch(double semitones);

    /// Renders until the ring holds the fill target, the sources ended, or nothing is loaded. Returns true when
    /// at least one chunk went out. The worker thread calls it; tests call it directly instead of start().
    bool fill();
    void start();
    void stop();

    [[nodiscard]] std::int64_t sourceFrame() const; // next frame the worker will render
    [[nodiscard]] std::int64_t lengthFrames() const; // longest source, device rate; 0 when nothing is loaded
    [[nodiscard]] bool ended() const;
    [[nodiscard]] std::size_t sourceCount() const;

private:
    struct Stream;
    struct Control {
        float gain = 1.0F;
        bool muted = false;
        bool solo = false;
    };
    struct Pending {
        std::vector<std::unique_ptr<Stream>> streams;
        bool replaceStreams = false;
        std::int64_t seekFrame = -1;
        std::int64_t loopFrom = 0;
        std::int64_t loopTo = 0;
        bool loopChanged = false;
        std::vector<Control> controls;
        bool controlsChanged = false;
        std::int64_t fillTarget = 0;
        double speed = 1.0;
        double pitch = 0.0;
        bool speedPitchChanged = false;
    };
    using Planar = std::array<std::span<float>, rt::stereoChannels>;

    void run(const std::stop_token &token);
    void applyPending();
    void applySpeedPitch();
    [[nodiscard]] bool stretchActive() const;
    bool renderChunk();
    int framesForNextChunk();
    int mixSources(int frames);
    void emitBypass(rt::Chunk &chunk, int frames);
    void emitStretched(rt::Chunk &chunk, int inFrames);
    bool flushTail(rt::Chunk &chunk, int inFrames);
    [[nodiscard]] std::int64_t stretchTag() const;
    void seekStreams(std::int64_t sourceFrame);
    static bool pullStream(Stream &stream, int frames);
    static bool readMore(Stream &stream);
    [[nodiscard]] std::size_t liveChunks() const;
    void mixStream(const Stream &stream, const Control &control, int frames);
    [[nodiscard]] bool anyoneSolo() const;

    rt::ChunkRing *m_ring;
    double m_deviceRate;
    std::vector<std::unique_ptr<Stream>> m_streams;
    std::vector<Control> m_controls;
    std::int64_t m_sourceFrame = 0;
    std::int64_t m_loopFrom = 0;
    std::int64_t m_loopTo = 0;
    std::int64_t m_fillTarget;
    double m_speed = 1.0;
    double m_pitch = 0.0;
    double m_carry = 0.0; // fractional source frames owed to the next chunk
    std::unique_ptr<Stretcher> m_stretcher; // created on first use, kept
    std::array<std::vector<float>, rt::stereoChannels> m_mix; // mixed source frames of one chunk, planar
    int m_flushRemaining =
        -1; // output frames still to drain from the stretcher after the sources ended (-1 = not flushing)
    std::uint32_t m_generation = 0;
    std::uint64_t m_popsAtSeek = 0;
    std::uint64_t m_discardsAtSeek = 0;
    std::uint64_t m_pushedSinceSeek = 0;
    bool m_ended = false;
    std::atomic<std::int64_t> m_sourceOut{0};
    std::atomic<bool> m_endedOut{false};
    std::atomic<std::int64_t> m_lengthOut{0};
    std::atomic<std::size_t> m_countOut{0};

    std::mutex m_mutex;
    Pending m_pending;
    std::jthread m_thread;
};

} // namespace cc::audio
