// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/shell/shell.h"

#include "core/project/predictioncache.h"
#include "ui/copy/projectsession.h"

#include <optional>

// Prediction persistence (copy-spec.md section 16.2 D-3): whenever a grey layer is shown for the active
// version it is written to cache/predictions/<source id>.json, and when a project opens or the active version
// changes the file for that version is read back and shown again, without a new stats event or status line.
// The key prediction is deliberately not kept (an inline offer, recomputed in a second).
namespace cc::ui
{

void Shell::wirePredictionCache()
{
    // after the clear* slots connected in wirePredictActions, so the reload lands on an empty state
    connect(m_project, &ProjectSession::projectChanged, this, &Shell::loadPredictionCache);
    connect(m_project, &ProjectSession::sourcesChanged, this, &Shell::loadPredictionCache);
}

void Shell::savePredictionCache(const model::Id &sourceId)
{
    project::PredictionCache cache;
    if (m_beatPrediction && m_beatPredictionSource == sourceId) {
        cache.beats = *m_beatPrediction;
    }
    if (m_noteEvents && m_notePredictionSource == sourceId) {
        cache.notes = *m_noteEvents;
    }
    if (m_chordSegments && m_chordPredictionSource == sourceId) {
        cache.chords = *m_chordSegments;
    }
    if (const Result<void> r = project::savePredictionCache(m_project->predictionCachePath(sourceId), cache); !r) {
        showStatus(tr("예측을 저장하지 못했습니다 (다시 열면 예측을 다시 돌려야 합니다)"));
    }
}

void Shell::loadPredictionCache()
{
    const auto sourceId = m_project->activeSourceId();
    if (!sourceId) {
        return;
    }
    const Result<project::PredictionCache> cache =
        project::loadPredictionCache(m_project->predictionCachePath(*sourceId));
    if (!cache) {
        return; // none yet, or unreadable: predict again
    }
    if (cache->beats) {
        showBeatPrediction(*cache->beats, *sourceId, true);
    }
    if (cache->notes) {
        showNotePrediction(*cache->notes, *sourceId, true);
    }
    if (cache->chords) {
        showChordPrediction(*cache->chords, *sourceId, true);
    }
}

} // namespace cc::ui
