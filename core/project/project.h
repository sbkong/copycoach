// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/model/id.h"
#include "core/model/score.h"
#include "core/model/timemap.h"
#include "core/theory/fingering.h"
#include "core/theory/pitch.h"
#include "core/theory/tuning.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The copy project (copy-spec.md section 1): the folder is the project, project.json holds everything below.
// Sections (the form) and the per-measure tempo live inside the score, nowhere else; the TimeMap is derived.
// Sync points are per source version. Field names are frozen by copy-spec.md section 10.
namespace cc::project
{

constexpr int projectSchemaVersion = 1;
constexpr std::string_view projectFileName = "project.json";
constexpr std::string_view autosaveFileName = "project.autosave.json";
constexpr int stageMin = 1; // copy-spec.md section 2: 1 materials ... 7 transcription
constexpr int stageMax = 7;
constexpr int speedPctMin = 25; // user decision 2026-11-03
constexpr int speedPctMax = 150;
constexpr int pitchSemitonesMax = 12;

enum class SourceKind : std::uint8_t {
    Audio,
    Video,
};

/// One recording or video of the song. Several versions may coexist; last_session.source_id names the active one.
struct Source {
    model::Id id;
    SourceKind kind = SourceKind::Audio;
    std::string path; // relative to the project folder (media/...)
    std::string hash; // content hash as hex, empty until computed
    int audioTrack = 0; // video: which audio stream
    std::string label;
};

/// What the recording is tuned to, as opposed to the track tuning the fretboard follows (tuning.md).
struct Reference {
    double pitchCents = 0.0; // A4 offset from 440 Hz
    theory::PitchClass tuningNote = 4; // the recording's lowest open string, E for standard pitch
};

/// "tick of the score sounds at time_s of source `sourceId`" (model::SyncPoint plus the version it belongs to).
struct SourceSyncPoint {
    model::Id sourceId;
    model::SyncPoint point{0, 0.0};
};

/// A saved loop range in measures (inclusive), with the playback settings it was practised at.
struct Range {
    model::Id id;
    std::string name;
    std::size_t barFrom = 0;
    std::size_t barTo = 0;
    int priority = 0;
    int speedPct = 100;
    int pitchSemitones = 0;
};

/// Saved vocabulary: a self-contained line (its own tuning, beats from tick 0) to view in the twelve keys.
struct Phrase {
    model::Id id;
    std::optional<model::Id> rangeId; // where it came from, if that range still exists
    theory::Tuning tuning;
    std::vector<model::Beat> beats; // tick = offset from the phrase start; track is unused (0)
    std::string chordContext; // "ii-V in F" or the chord symbols under the line
    theory::FingeringPattern fingeringPattern = theory::FingeringPattern::Vertical;
    std::vector<theory::PitchClass> keysDone;
};

/// The two memo anchors (copy-spec.md section 7-6): the song, and each saved range.
struct Memos { // NOLINT(bugprone-exception-escape) plain aggregate; string members may allocate
    std::string project;
    std::map<model::Id, std::string> ranges;
};

/// Where to resume when the project opens. Position is a tick, not seconds: versions have different sync.
struct LastSession {
    std::optional<model::Id> rangeId;
    model::Tick playheadTick = 0;
    int speedPct = 100;
    std::optional<model::Id> sourceId;
    std::string savedAt; // ISO 8601 UTC, "2026-11-04T09:15:00Z"
};

struct Project { // NOLINT(bugprone-exception-escape) plain aggregate; string members may allocate
    int schemaVersion = projectSchemaVersion;
    std::string projectId; // UUID v4
    std::vector<Source> sources;
    Reference reference;
    model::Score score;
    std::vector<SourceSyncPoint> syncPoints;
    std::vector<Range> ranges;
    std::vector<Phrase> phrases;
    Memos memos;
    std::string lyrics;
    int stage = stageMin;
    LastSession lastSession;
};

/// A new, empty project: fresh id, stage 1, one standard-tuned guitar track and no measures.
Project newProject();

/// The sync points of one source version, in file order, ready for model::TimeMap::fromSyncPoints.
std::vector<model::SyncPoint> syncPointsFor(const Project &project, const model::Id &sourceId);

} // namespace cc::project
