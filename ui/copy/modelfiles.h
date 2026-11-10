// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/jobs/job.h"
#include "core/predict/beatthis.h"
#include "core/predict/beatthismodel.h"
#include "ui/shell/apppaths.h"

#include <QUrl>

#include <filesystem>
#include <span>
#include <vector>

// Model files on this machine (states.md section 2 "모델 가중치 다운로드"): kept under the user's data folder,
// never bundled, fetched on first use from the project's GitHub release and checked by size and SHA-256.
namespace cc::ui
{

/// <user data>/models/beat_this
[[nodiscard]] std::filesystem::path beatThisDir(const AppPaths &paths);
/// <user data>/models/chordmini
[[nodiscard]] std::filesystem::path chordMiniDir(const AppPaths &paths);
/// <user data>/models/spleeter (downloaded) and models/htdemucs (the user's own research-only conversion)
[[nodiscard]] std::filesystem::path spleeterDir(const AppPaths &paths);
[[nodiscard]] std::filesystem::path htdemucsDir(const AppPaths &paths);
[[nodiscard]] std::filesystem::path modelPath(const std::filesystem::path &dir, const predict::ModelFile &file);
/// Present and of the published size (the hash is checked once, when the download lands).
[[nodiscard]] bool modelPresent(const std::filesystem::path &dir, const predict::ModelFile &file);
[[nodiscard]] std::vector<predict::ModelFile> missingModels(const std::filesystem::path &dir,
                                                            std::span<const predict::ModelFile> files);
[[nodiscard]] predict::BeatThisFiles beatThisFiles(const std::filesystem::path &dir, predict::BeatThisVariant variant);

/// Fetches `files` from `baseUrl` + name into `dir` as a job (JobKind::ModelDownload): each file lands as
/// name.part, is hashed, then renamed; a mismatch is ModelCorrupt, a network problem DownloadFailed, a stop
/// request Cancelled. Progress is bytes over the published total.
[[nodiscard]] jobs::Work
downloadModels(const QUrl &baseUrl, std::filesystem::path dir, std::vector<predict::ModelFile> files);

} // namespace cc::ui
