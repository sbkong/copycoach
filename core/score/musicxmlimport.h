// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"
#include "core/model/id.h"
#include "core/model/score.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// MusicXML (partwise) -> the event model, our own parser over pugixml (score-data-path.md section 8): what the
// copy workflow needs from a Guitar Pro / MuseScore export. Parts become tracks; the TAB staff of a part is the
// source of its notes (string and fret as written), a part without one takes staff 1 and computes a position
// from the pitch. Per-measure meter, key, tempo and swing changes, chord symbols (harmony), rehearsal marks
// (sections) and free text (words, for the memos of copy-spec.md section 9) come along. Not read: grace notes,
// repeats, articulations and lyrics (editor pieces), compressed .mxl.
namespace cc::score
{

/// A direction's text with where it stood: tick inside the measure.
struct ImportedText {
    std::size_t measure = 0;
    model::Tick tick = 0;
    std::string text;
};

struct ScoreImport {
    model::Score score;
    std::string title; // work-title or movement-title
    std::vector<std::string> credits; // credit-words, in file order
    std::vector<ImportedText> texts;
    std::vector<std::string> warnings; // log text, one per thing dropped or guessed
};

Result<ScoreImport> importMusicXml(const std::filesystem::path &path, model::IdGenerator &ids);
/// `path` only names the document in errors and warnings.
Result<ScoreImport>
importMusicXmlData(std::string_view xml, const std::filesystem::path &path, model::IdGenerator &ids);

} // namespace cc::score
