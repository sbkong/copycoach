// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/copy/modelfiles.h"

#include <QCryptographicHash>
#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>

namespace cc::ui
{

namespace
{

constexpr int stopPollMs = 100;

QString qpath(const std::filesystem::path &path)
{
    return QString::fromStdWString(path.wstring());
}

std::string ascii(std::string_view view)
{
    return std::string(view);
}

QString qname(std::string_view view)
{
    return QString::fromUtf8(view.data(), static_cast<qsizetype>(view.size()));
}

/// One file over the worker's own event loop: bytes go to the .part file and the hash as they arrive; the
/// stop token is polled every 100 ms and aborts the reply.
class Transfer
{
public:
    explicit Transfer(const std::filesystem::path &part)
        : m_opened(open(part))
    {
    }

    [[nodiscard]] bool opened() const
    {
        return m_opened;
    }

    /// Blocks until the reply finishes or is aborted; the reply's error state tells which.
    void run(QNetworkReply &reply, const std::stop_token &stop, const jobs::Report &onBytes)
    {
        QEventLoop loop;
        QObject::connect(&reply, &QNetworkReply::readyRead, &loop, [this, &reply, &onBytes] {
            const QByteArray bytes = reply.readAll();
            m_file.write(bytes);
            m_hash.addData(bytes);
            m_received += static_cast<std::uint64_t>(bytes.size());
            onBytes(static_cast<double>(m_received));
        });
        QObject::connect(&reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer poll;
        QObject::connect(&poll, &QTimer::timeout, &loop, [&reply, &stop] {
            if (stop.stop_requested()) {
                reply.abort();
            }
        });
        poll.start(stopPollMs);
        loop.exec();
        m_file.close();
    }

    [[nodiscard]] bool matches(const predict::ModelFile &model) const
    {
        return m_received == model.size && m_hash.result().toHex().toStdString() == model.sha256;
    }

private:
    bool open(const std::filesystem::path &part)
    {
        m_file.setFileName(qpath(part));
        return m_file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    }

    QFile m_file;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    std::uint64_t m_received = 0;
    bool m_opened;
};

/// The whole job's state: one manager on the worker thread, bytes landed so far for the progress fraction.
class Downloader
{
public:
    Downloader(QUrl baseUrl, std::filesystem::path dir, std::span<const predict::ModelFile> files)
        : m_baseUrl(std::move(baseUrl))
        , m_dir(std::move(dir))
        , m_total(totalSize(files))
    {
    }

    /// name.part fetched, checked and renamed into place; on any failure the partial file is removed
    /// (states.md section 2).
    [[nodiscard]] Result<void>
    land(const predict::ModelFile &file, const std::stop_token &stop, const jobs::Report &report)
    {
        const std::filesystem::path final = modelPath(m_dir, file);
        const std::filesystem::path part = final.string() + ".part";
        std::error_code ec;
        const Result<void> fetched = fetch(file, part, stop, report);
        if (!fetched) {
            std::filesystem::remove(part, ec);
            return fetched;
        }
        std::filesystem::rename(part, final, ec);
        if (ec) {
            return fail(ErrorCode::WriteFailed, final.string(), ec.message());
        }
        m_done += file.size;
        return {};
    }

private:
    [[nodiscard]] Result<void> fetch(const predict::ModelFile &file,
                                     const std::filesystem::path &part,
                                     const std::stop_token &stop,
                                     const jobs::Report &report)
    {
        Transfer transfer(part);
        if (!transfer.opened()) {
            return fail(ErrorCode::WriteFailed, part.string());
        }
        const QUrl url = m_baseUrl.resolved(QUrl(qname(file.name)));
        const std::unique_ptr<QNetworkReply> reply(m_net.get(QNetworkRequest(url)));
        transfer.run(*reply, stop, [this, &report](double bytes) {
            report((static_cast<double>(m_done) + bytes) / static_cast<double>(m_total));
        });
        if (stop.stop_requested()) {
            return fail(ErrorCode::Cancelled, ascii(file.name));
        }
        if (reply->error() != QNetworkReply::NoError) {
            return fail(ErrorCode::DownloadFailed, ascii(file.name), reply->errorString().toStdString());
        }
        if (!transfer.matches(file)) {
            return fail(ErrorCode::ModelCorrupt, ascii(file.name), "size or SHA-256 differs from the release manifest");
        }
        return {};
    }

    static std::uint64_t totalSize(std::span<const predict::ModelFile> files)
    {
        std::uint64_t total = 0;
        for (const predict::ModelFile &file : files) {
            total += file.size;
        }
        return std::max<std::uint64_t>(1, total);
    }

    QNetworkAccessManager m_net;
    QUrl m_baseUrl;
    std::filesystem::path m_dir;
    std::uint64_t m_total;
    std::uint64_t m_done = 0;
};

} // namespace

std::filesystem::path beatThisDir(const AppPaths &paths)
{
    return paths.userDir / "models" / "beat_this";
}

std::filesystem::path chordMiniDir(const AppPaths &paths)
{
    return paths.userDir / "models" / "chordmini";
}

std::filesystem::path spleeterDir(const AppPaths &paths)
{
    return paths.userDir / "models" / "spleeter";
}

std::filesystem::path htdemucsDir(const AppPaths &paths)
{
    return paths.userDir / "models" / "htdemucs";
}

std::filesystem::path modelPath(const std::filesystem::path &dir, const predict::ModelFile &file)
{
    return dir / std::filesystem::path(ascii(file.name));
}

bool modelPresent(const std::filesystem::path &dir, const predict::ModelFile &file)
{
    std::error_code ec;
    const std::filesystem::path path = modelPath(dir, file);
    return std::filesystem::is_regular_file(path, ec) && std::filesystem::file_size(path, ec) == file.size;
}

std::vector<predict::ModelFile> missingModels(const std::filesystem::path &dir,
                                              std::span<const predict::ModelFile> files)
{
    std::vector<predict::ModelFile> missing;
    for (const predict::ModelFile &file : files) {
        if (!modelPresent(dir, file)) {
            missing.push_back(file);
        }
    }
    return missing;
}

predict::BeatThisFiles beatThisFiles(const std::filesystem::path &dir, predict::BeatThisVariant variant)
{
    return {modelPath(dir, predict::beatThisModel(variant)), modelPath(dir, predict::beatThisFilterbank)};
}

namespace
{

Result<void> runDownload(const QUrl &baseUrl,
                         const std::filesystem::path &dir,
                         std::span<const predict::ModelFile> files,
                         const std::stop_token &stop,
                         const jobs::Report &report)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return fail(ErrorCode::WriteFailed, dir.string(), ec.message());
    }
    Downloader downloader(baseUrl, dir, files);
    for (const predict::ModelFile &file : files) {
        if (Result<void> landed = downloader.land(file, stop, report); !landed) {
            return landed;
        }
    }
    report(1.0);
    return {};
}

} // namespace

jobs::Work downloadModels(const QUrl &baseUrl, std::filesystem::path dir, std::vector<predict::ModelFile> files)
{
    return [baseUrl, dir = std::move(dir), files = std::move(files)](const std::stop_token &stop,
                                                                     const jobs::Report &report) {
        return runDownload(baseUrl, dir, files, stop, report);
    };
}

} // namespace cc::ui
