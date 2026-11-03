// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/audio/rt/chunkring.h"
#include "core/error.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
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
    };

    void run(const std::stop_token &token);
    void applyPending();
    bool renderChunk();
    int framesForNextChunk();
    void seekStreams(std::int64_t sourceFrame);
    static bool pullStream(Stream &stream, int frames);
    static bool readMore(Stream &stream);
    [[nodiscard]] std::size_t liveChunks() const;
    void mixStream(const Stream &stream, const Control &control, rt::Chunk &chunk, int frames) const;
    [[nodiscard]] bool anyoneSolo() const;

    rt::ChunkRing *m_ring;
    double m_deviceRate;
    std::vector<std::unique_ptr<Stream>> m_streams;
    std::vector<Control> m_controls;
    std::int64_t m_sourceFrame = 0;
    std::int64_t m_loopFrom = 0;
    std::int64_t m_loopTo = 0;
    std::int64_t m_fillTarget;
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
