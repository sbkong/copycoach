// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/separation/separator.h"

#include "core/media/audiofile.h"
#include "core/media/loadaudio.h"
#include "core/separation/htdemucs.h"
#include "core/separation/roformer.h"
#include "core/separation/spleeter.h"

#include <array>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

namespace cc::separation
{

namespace
{

constexpr double decodeShare = 0.1;

std::filesystem::path stemPath(const std::filesystem::path &dir, std::size_t stem)
{
    return dir / (std::string(stemNames[stem]) + ".flac");
}

/// One FLAC writer per stem, opened when the engine first delivers that stem: whether four or six come is
/// the graph's decision, known only once it runs.
class StemFiles
{
public:
    explicit StemFiles(std::filesystem::path outDir)
        : m_outDir(std::move(outDir))
    {
    }

    [[nodiscard]] Result<void> write(std::size_t stem, std::span<const float> interleaved)
    {
        if (stem >= maxStemCount) {
            return fail(ErrorCode::ModelCorrupt, m_outDir.string(), "stem " + std::to_string(stem));
        }
        if (!m_writers[stem]) {
            auto writer = std::make_unique<media::AudioFileWriter>();
            if (Result<void> r = writer->open(stemPath(m_outDir, stem),
                                              stemSampleRate,
                                              stemChannels,
                                              media::AudioFileFormat::Flac16);
                !r) {
                return r;
            }
            m_writers[stem] = std::move(writer);
        }
        return m_writers[stem]->write(interleaved);
    }

    /// Closes the writers and removes stale files of the stems this run did not deliver; the paths written,
    /// in stemNames order.
    [[nodiscard]] Result<std::vector<std::filesystem::path>> close()
    {
        std::vector<std::filesystem::path> paths;
        std::error_code ec;
        for (std::size_t k = 0; k < maxStemCount; ++k) {
            if (!m_writers[k]) {
                std::filesystem::remove(stemPath(m_outDir, k), ec);
                continue;
            }
            if (Result<void> r = m_writers[k]->close(); !r) {
                return std::unexpected(r.error());
            }
            paths.push_back(stemPath(m_outDir, k));
        }
        return paths;
    }

    /// Drops the writers and deletes every stem file.
    void removeAll()
    {
        m_writers = {};
        std::error_code ec;
        for (std::size_t k = 0; k < maxStemCount; ++k) {
            std::filesystem::remove(stemPath(m_outDir, k), ec);
        }
    }

private:
    std::filesystem::path m_outDir;
    std::array<std::unique_ptr<media::AudioFileWriter>, maxStemCount> m_writers;
};

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
    if (model.engine == StemEngine::Roformer) {
        return separateRoformer(*interleaved, model.path, sink, stop, engineReport);
    }
    if (model.engine == StemEngine::Htdemucs) {
        return separateHtdemucs(*interleaved, model.path, sink, stop, engineReport);
    }
    return separateSpleeter(*interleaved, model.path, sink, stop, engineReport);
}

Result<std::vector<std::filesystem::path>> separateToFiles(const std::filesystem::path &audio,
                                                           const StemModel &model,
                                                           const std::filesystem::path &outDir,
                                                           const std::stop_token &stop,
                                                           const jobs::Report &report)
{
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    StemFiles files(outDir);
    const StemSink sink = [&files](std::size_t stem, std::span<const float> interleaved) -> Result<void> {
        return files.write(stem, interleaved);
    };
    if (Result<void> r = separate(audio, model, sink, stop, report); !r) {
        files.removeAll();
        return std::unexpected(r.error());
    }
    Result<std::vector<std::filesystem::path>> paths = files.close();
    if (!paths) {
        files.removeAll();
    }
    return paths;
}

std::optional<std::vector<std::filesystem::path>> cachedStemFiles(const std::filesystem::path &dir)
{
    std::vector<std::filesystem::path> paths;
    for (std::size_t k = 0; k < maxStemCount; ++k) {
        std::filesystem::path path = stemPath(dir, k);
        if (!std::filesystem::exists(path)) {
            break;
        }
        paths.push_back(std::move(path));
    }
    if (paths.size() == maxStemCount) {
        return paths;
    }
    if (paths.size() >= baseStemCount) {
        paths.resize(baseStemCount); // a guitar file without its piano is not a six-stem set
        return paths;
    }
    return std::nullopt;
}

} // namespace cc::separation
