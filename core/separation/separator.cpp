// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/separation/separator.h"

#include "core/media/audiofile.h"
#include "core/media/loadaudio.h"
#include "core/separation/htdemucs.h"
#include "core/separation/spleeter.h"

#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace cc::separation
{

namespace
{

constexpr double decodeShare = 0.1;

std::array<std::filesystem::path, stemCount> stemPaths(const std::filesystem::path &outDir)
{
    std::array<std::filesystem::path, stemCount> paths;
    for (std::size_t k = 0; k < stemCount; ++k) {
        paths[k] = outDir / (std::string(stemNames[k]) + ".flac");
    }
    return paths;
}

void removeAll(const std::array<std::filesystem::path, stemCount> &paths)
{
    std::error_code ec;
    for (const std::filesystem::path &path : paths) {
        std::filesystem::remove(path, ec);
    }
}

} // namespace

Result<void> separate(const std::filesystem::path &audio,
                      const StemModel &model,
                      const StemSink &sink,
                      const std::stop_token &stop,
                      const jobs::Report &report)
{
    Result<std::vector<float>> interleaved =
        media::loadResampled(audio, stemSampleRate, stemChannels, stop, [&report](double p) {
            report(decodeShare * p);
        });
    if (!interleaved) {
        return std::unexpected(interleaved.error());
    }
    const jobs::Report engineReport = [&report](double p) {
        report(decodeShare + ((1.0 - decodeShare) * p));
    };
    if (model.engine == StemEngine::Htdemucs) {
        return separateHtdemucs(*interleaved, model.path, sink, stop, engineReport);
    }
    return separateSpleeter(*interleaved, model.path, sink, stop, engineReport);
}

Result<std::array<std::filesystem::path, stemCount>> separateToFiles(const std::filesystem::path &audio,
                                                                     const StemModel &model,
                                                                     const std::filesystem::path &outDir,
                                                                     const std::stop_token &stop,
                                                                     const jobs::Report &report)
{
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    std::array<std::filesystem::path, stemCount> paths = stemPaths(outDir);
    std::array<std::unique_ptr<media::AudioFileWriter>, stemCount> writers;
    for (std::size_t k = 0; k < stemCount; ++k) {
        writers[k] = std::make_unique<media::AudioFileWriter>();
        if (Result<void> r = writers[k]->open(paths[k], stemSampleRate, stemChannels, media::AudioFileFormat::Flac16);
            !r) {
            removeAll(paths);
            return std::unexpected(r.error());
        }
    }
    const StemSink sink = [&writers](std::size_t stem, std::span<const float> interleaved) -> Result<void> {
        return writers[stem]->write(interleaved);
    };
    if (Result<void> r = separate(audio, model, sink, stop, report); !r) {
        writers = {};
        removeAll(paths);
        return std::unexpected(r.error());
    }
    for (const std::unique_ptr<media::AudioFileWriter> &writer : writers) {
        if (Result<void> r = writer->close(); !r) {
            removeAll(paths);
            return std::unexpected(r.error());
        }
    }
    return paths;
}

} // namespace cc::separation
