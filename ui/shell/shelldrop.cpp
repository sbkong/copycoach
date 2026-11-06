// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/shell/shell.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>

#include <algorithm>
#include <array>
#include <cctype>
#include <span>
#include <string_view>

// Drop anywhere on the window = file.import (copy-spec.md section 16.2 C-3, states.md section 3): a recording
// or a MusicXML file, told apart by the extension; several files import one after the other.
namespace cc::ui
{

namespace
{

constexpr std::array<std::string_view, 2> scoreExtensions{".musicxml", ".xml"};
constexpr std::array<std::string_view, 4> audioExtensions{".wav", ".flac", ".mp3", ".ogg"};
constexpr std::array<std::string_view, 4> videoExtensions{".mp4", ".mkv", ".mov", ".webm"}; // video.md section 2

std::string lowerExtension(const std::filesystem::path &path)
{
    std::string ext = path.extension().string();
    for (char &c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext;
}

bool among(const std::string &ext, std::span<const std::string_view> list)
{
    return std::ranges::any_of(list, [&ext](std::string_view each) {
        return each == ext;
    });
}

} // namespace

bool Shell::isImportable(const std::filesystem::path &path)
{
    const std::string ext = lowerExtension(path);
    return among(ext, scoreExtensions) || among(ext, audioExtensions) || among(ext, videoExtensions);
}

/// One file by its extension: scores through the importer, recordings as a source version.
void Shell::importPath(const std::filesystem::path &path)
{
    const std::string ext = lowerExtension(path);
    if (among(ext, scoreExtensions)) {
        importScore(path);
    } else if (among(ext, videoExtensions)) {
        importVideo(path);
    } else {
        importAudio(path);
    }
}

void Shell::dragEnterEvent(QDragEnterEvent *event)
{
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile() && isImportable(std::filesystem::path(url.toLocalFile().toStdWString()))) {
            event->acceptProposedAction();
            return;
        }
    }
    event->ignore();
}

void Shell::dropEvent(QDropEvent *event)
{
    bool any = false;
    for (const QUrl &url : event->mimeData()->urls()) {
        const std::filesystem::path path(url.toLocalFile().toStdWString());
        if (url.isLocalFile() && isImportable(path)) {
            importPath(path);
            any = true;
        }
    }
    if (any) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

} // namespace cc::ui
