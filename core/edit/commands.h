// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/edit/command.h"
#include "core/model/prediction.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Command factories: each builds a command against the current model and returns it unapplied, for
// UndoStack::push. A null result means the edit would change nothing. Measure indices and ticks come from the
// cursor and are asserted, not validated; a string or fret outside the tuning or the limits is rejected (null)
// so a typed digit can never put an unplayable note into the model.
namespace cc::edit
{

/// A beat position: the measure, the track, the voice and the tick inside the measure.
struct BeatRef {
    std::size_t measure = 0;
    std::size_t track = 0;
    int voice = 1;
    model::Tick tick = 0;
};

constexpr int maxFret = 36;

// --- beats and notes (editor.input.*, editor.rest, editor.duration.*, editor.dot, editor.tuplet, editor.tie.*,
// editor.delete.*). Writing a note by hand in the copy workflow (copy-spec.md section 7) is setNote.

/// Puts the note at the position; creates the beat with `dur` when none is there, a rest becomes a note, a note
/// already on that string changes its fret.
std::unique_ptr<Command>
setNote(const model::Score &score, BeatRef at, theory::StringFret note, model::Dur dur, model::IdGenerator &ids);
/// Removes the note on that string; the beat turns into a rest when it was the last one.
std::unique_ptr<Command> removeNote(const model::Score &score, BeatRef at, int string);
/// Makes the beat a rest of `dur` (creating it when absent); its notes are dropped.
std::unique_ptr<Command> setRest(const model::Score &score, BeatRef at, model::Dur dur, model::IdGenerator &ids);
std::unique_ptr<Command> deleteBeat(const model::Score &score, BeatRef at);
std::unique_ptr<Command>
setBeatLength(const model::Score &score, BeatRef at, model::Dur dur, int dots, std::optional<model::Tuplet> tuplet);
std::unique_ptr<Command> setTie(const model::Score &score, BeatRef at, int string, bool tied);

// --- measure attributes (nullopt clears a change so the previous value flows on) and chord symbols.

std::unique_ptr<Command> setTempo(const model::Score &score, std::size_t measure, std::optional<double> bpm);
std::unique_ptr<Command>
setTimeSig(const model::Score &score, std::size_t measure, std::optional<model::TimeSignature> timeSig);
std::unique_ptr<Command>
setKeySig(const model::Score &score, std::size_t measure, std::optional<model::KeySignature> keySig);
std::unique_ptr<Command> setSwing(const model::Score &score, std::size_t measure, std::optional<double> ratio);
/// Writes the symbol at the tick (replacing one already there); empty text removes it.
std::unique_ptr<Command> setChordSymbol(const model::Score &score,
                                        std::size_t measure,
                                        model::Tick tick,
                                        const std::string &text,
                                        model::IdGenerator &ids);

// --- score structure. Inserting or deleting measures moves everything anchored on measures or absolute ticks
// with them: sections, saved ranges (clipped; dropped with their memo and phrase links when fully deleted) and
// sync points (dropped inside a deleted span).

std::unique_ptr<Command>
insertMeasures(const project::Project &project, std::size_t at, std::size_t count, model::IdGenerator &ids);
std::unique_ptr<Command> deleteMeasures(const project::Project &project, std::size_t at, std::size_t count);
/// editor.section.edit: the section starting at the measure; both texts empty removes it.
std::unique_ptr<Command> setSection(const model::Score &score,
                                    std::size_t measure,
                                    std::string letter,
                                    std::string name,
                                    model::IdGenerator &ids);
std::unique_ptr<Command> setTrack(const model::Score &score, std::size_t index, model::Track track);

// --- prediction layer (copy-spec.md section 4): copy.predict.accept. Notes items that carry a position become
// notes of `track` (voice 1, the plain note value nearest to the item length); Chords items become chord
// symbols. Beats, Key and Fingering items have no accept path yet (sync points and the key are edited directly).

/// Measures [from, to], inclusive.
struct MeasureSpan {
    std::size_t from = 0;
    std::size_t to = 0;
};

std::unique_ptr<Command> acceptPrediction(const project::Project &project,
                                          const model::Prediction &prediction,
                                          MeasureSpan measures,
                                          std::size_t track,
                                          model::IdGenerator &ids);

// --- project fields outside the score (copy-spec.md section 1): memos, lyrics, ranges, sync points, reference.

std::unique_ptr<Command> setProjectMemo(const project::Project &project, std::string text);
std::unique_ptr<Command> setRangeMemo(const project::Project &project, const model::Id &rangeId, std::string text);
std::unique_ptr<Command> setLyrics(const project::Project &project, std::string text);
/// Adds the range, or replaces the one with the same id.
std::unique_ptr<Command> setRange(const project::Project &project, project::Range range);
/// Removes the range with its memo; phrases and the last session forget it.
std::unique_ptr<Command> removeRange(const project::Project &project, const model::Id &rangeId);
/// Replaces every sync point of one source version.
std::unique_ptr<Command>
setSyncPoints(const project::Project &project, const model::Id &sourceId, std::vector<model::SyncPoint> points);
std::unique_ptr<Command> setReference(const project::Project &project, project::Reference reference);

} // namespace cc::edit
