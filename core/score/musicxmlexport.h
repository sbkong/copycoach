// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"
#include "core/model/score.h"

#include <filesystem>
#include <string>

// The model as MusicXML 4.0 partwise (score-data-path.md section 8, the way back to Guitar Pro): one part per
// track with a staff + TAB pair, divisions = ticks per quarter so durations are exact, string/fret on every
// note, chord symbols as <harmony> with the kind's text (our spelling, root and bass split out), sections as
// rehearsal marks, tempo and swing as <sound>, key and meter changes in the measure they happen. Everything
// the importer reads comes back the same (musicxmlexport_test.cpp round-trips the import fixture).
namespace cc::score
{

[[nodiscard]] std::string exportMusicXml(const model::Score &score, const std::string &title);
/// WriteFailed when the file cannot be written.
[[nodiscard]] Result<void>
writeMusicXml(const std::filesystem::path &path, const model::Score &score, const std::string &title);

} // namespace cc::score
