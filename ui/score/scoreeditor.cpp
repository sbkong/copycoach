// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/score/scoreeditor.h"

#include "core/edit/commands.h"
#include "core/model/locate.h"
#include "core/project/projectfile.h"

#include <algorithm>
#include <map>
#include <vector>

namespace cc::ui
{

namespace
{

constexpr std::size_t maxPendingDigits = 2;

edit::BeatRef refOf(const BeatLocation &at)
{
    return edit::BeatRef{at.measure, at.track, at.voice, at.tick};
}

/// Start ticks of the voice's beats in a measure, ascending.
std::vector<model::Tick> ticksOf(const model::Measure &measure, std::size_t track, int voice)
{
    std::vector<model::Tick> ticks;
    for (const model::Beat &beat : measure.beats) {
        if (beat.track == track && beat.voice == voice) {
            ticks.push_back(beat.tick);
        }
    }
    return ticks;
}

const model::Beat *beatIn(const model::Measure &measure, const BeatLocation &at)
{
    for (const model::Beat &beat : measure.beats) {
        if (beat.track == at.track && beat.voice == at.voice && beat.tick == at.tick) {
            return &beat;
        }
    }
    return nullptr;
}

model::Dur stepDur(model::Dur dur, int delta)
{
    const int index = std::clamp(static_cast<int>(dur) + delta,
                                 static_cast<int>(model::Dur::Whole),
                                 static_cast<int>(model::Dur::SixtyFourth));
    return static_cast<model::Dur>(index);
}

} // namespace

ScoreEditor::ScoreEditor(QObject *parent)
    : QObject(parent)
    , m_project(project::newProject())
{
}

void ScoreEditor::setScore(const model::Score &score)
{
    project::Project project = project::newProject();
    project.score = score;
    setProject(std::move(project));
}

void ScoreEditor::setProject(project::Project project)
{
    m_project = std::move(project);
    m_hasProject = true;
    m_undo.clear();
    m_pending.clear();
    m_cursor.reset();
    if (!m_project.score.measures.empty() && !m_project.score.tracks.empty()) {
        m_cursor = barEdge(BeatLocation{0, 0, 1, 0}, 0, false);
    }
    emit scoreChanged();
    emit cursorChanged();
}

void ScoreEditor::clearProject()
{
    m_project = project::newProject();
    m_hasProject = false;
    m_undo.clear();
    m_pending.clear();
    m_cursor.reset();
    emit scoreChanged();
    emit cursorChanged();
}

bool ScoreEditor::hasProject() const
{
    return m_hasProject;
}

void ScoreEditor::replaceScore(const model::Score &score)
{
    m_hasProject = true;
    m_cursor.reset();
    m_pending.clear();
    if (!run(edit::replaceScore(m_project, score))) {
        emit scoreChanged();
    }
    if (!m_project.score.measures.empty() && !m_project.score.tracks.empty()) {
        m_cursor = barEdge(BeatLocation{0, 0, 1, 0}, 0, false);
    }
    emit cursorChanged();
}

void ScoreEditor::storeLastSession()
{
    if (!m_cursor) {
        return;
    }
    m_project.lastSession.playheadTick = score().measureStartTick(m_cursor->measure) + m_cursor->tick;
    m_project.lastSession.savedAt = project::nowIso8601Utc();
}

/// The cursor goes to the beat under last_session.playhead_tick on track 0, when the score has it.
void ScoreEditor::restoreLastSession()
{
    const auto located = model::locateBeat(score(), m_project.lastSession.playheadTick, 0);
    if (located) {
        m_cursor = BeatLocation{located->measure, 0, located->beat->voice, located->beat->tick};
        emit cursorChanged();
    }
}

const model::Score &ScoreEditor::score() const
{
    return m_project.score;
}

const project::Project &ScoreEditor::project() const
{
    return m_project;
}

edit::UndoStack &ScoreEditor::undoStack()
{
    return m_undo;
}

void ScoreEditor::setCursor(std::optional<BeatLocation> cursor)
{
    if (cursor == m_cursor) {
        return;
    }
    m_cursor = cursor;
    m_pending.clear();
    emit cursorChanged();
}

std::optional<BeatLocation> ScoreEditor::cursor() const
{
    return m_cursor;
}

int ScoreEditor::cursorString() const
{
    return m_string;
}

model::Dur ScoreEditor::inputDur() const
{
    return m_inputDur;
}

const std::string &ScoreEditor::pendingDigits() const
{
    return m_pending;
}

/// The cursor as a command position; callers hold a beat from beatAtCursor(), so the cursor is set.
edit::BeatRef ScoreEditor::cursorRef() const
{
    return refOf(m_cursor.value_or(BeatLocation{}));
}

const model::Beat *ScoreEditor::beatAtCursor() const
{
    if (!m_cursor || m_cursor->measure >= score().measures.size()) {
        return nullptr;
    }
    return beatIn(score().measures[m_cursor->measure], *m_cursor);
}

// --- cursor movement ---------------------------------------------------------------------------------------

/// The next beat of the voice; after the last beat of a measure an empty slot where it ends (if the measure
/// has room), else the first beat of the next measure. Stays put at the end of the score.
std::optional<BeatLocation> ScoreEditor::stepRight(const BeatLocation &from) const
{
    const model::Score &s = score();
    const std::vector<model::Tick> ticks = ticksOf(s.measures[from.measure], from.track, from.voice);
    const auto next = std::ranges::upper_bound(ticks, from.tick);
    if (next != ticks.end()) {
        return BeatLocation{from.measure, from.track, from.voice, *next};
    }
    if (const model::Beat *beat = beatIn(s.measures[from.measure], from); beat != nullptr) {
        const model::Tick end = from.tick + beat->ticks();
        if (end < model::measureTicks(s.timeSigAt(from.measure))) {
            return BeatLocation{from.measure, from.track, from.voice, end};
        }
    }
    if (from.measure + 1 < s.measures.size()) {
        return barEdge(from, from.measure + 1, false);
    }
    return std::nullopt;
}

std::optional<BeatLocation> ScoreEditor::stepLeft(const BeatLocation &from) const
{
    const model::Score &s = score();
    const std::vector<model::Tick> ticks = ticksOf(s.measures[from.measure], from.track, from.voice);
    const auto here = std::ranges::lower_bound(ticks, from.tick);
    if (here != ticks.begin()) {
        return BeatLocation{from.measure, from.track, from.voice, *std::prev(here)};
    }
    if (from.measure > 0) {
        return barEdge(from, from.measure - 1, true);
    }
    return std::nullopt;
}

/// First (or last) beat of the voice in the measure; tick 0 when the voice has none there.
BeatLocation ScoreEditor::barEdge(const BeatLocation &from, std::size_t measure, bool end) const
{
    const std::vector<model::Tick> ticks = ticksOf(score().measures[measure], from.track, from.voice);
    if (ticks.empty()) {
        return BeatLocation{measure, from.track, from.voice, 0};
    }
    return BeatLocation{measure, from.track, from.voice, end ? ticks.back() : ticks.front()};
}

/// Same measure on another track, on the last beat starting at or before the current tick.
BeatLocation ScoreEditor::snapToTrack(const BeatLocation &from, std::size_t track) const
{
    const std::vector<model::Tick> ticks = ticksOf(score().measures[from.measure], track, from.voice);
    const auto after = std::ranges::upper_bound(ticks, from.tick);
    const model::Tick tick = after == ticks.begin() ? 0 : *std::prev(after);
    return BeatLocation{from.measure, track, from.voice, tick};
}

void ScoreEditor::setStringClamped(int string)
{
    const int count = m_cursor ? score().tracks[m_cursor->track].tuning.stringCount() : 1;
    const int clamped = std::clamp(string, 1, std::max(1, count));
    if (clamped == m_string) {
        return;
    }
    m_string = clamped;
    m_pending.clear();
    emit cursorChanged();
}

void ScoreEditor::move(Move how)
{
    if (!m_cursor) {
        return;
    }
    const BeatLocation at = *m_cursor;
    const std::size_t lastMeasure = score().measures.size() - 1;
    switch (how) {
    case Move::Left:
        setCursor(stepLeft(at).value_or(at));
        break;
    case Move::Right:
        setCursor(stepRight(at).value_or(at));
        break;
    case Move::Up:
        setStringClamped(m_string - 1);
        break;
    case Move::Down:
        setStringClamped(m_string + 1);
        break;
    case Move::BarStart:
        setCursor(barEdge(at, at.measure, false));
        break;
    case Move::BarEnd:
        setCursor(barEdge(at, at.measure, true));
        break;
    case Move::FirstBar:
        setCursor(barEdge(at, 0, false));
        break;
    case Move::LastBar:
        setCursor(barEdge(at, lastMeasure, false));
        break;
    case Move::TrackPrev:
        setCursor(snapToTrack(at, at.track > 0 ? at.track - 1 : at.track));
        break;
    case Move::TrackNext:
        setCursor(snapToTrack(at, std::min(at.track + 1, score().tracks.size() - 1)));
        break;
    }
}

// --- edits ------------------------------------------------------------------------------------------------

bool ScoreEditor::run(std::unique_ptr<edit::Command> command)
{
    if (!command) {
        return false;
    }
    m_undo.push(m_project, std::move(command));
    emit scoreChanged();
    return true;
}

/// Digits accumulate into one fret (Guitar Pro style): the second digit replaces the note the first one
/// wrote, so the history holds one command per fret.
void ScoreEditor::typeDigit(int digit)
{
    if (!m_cursor || digit < 0 || digit > 9) {
        return;
    }
    std::string digits = m_pending.size() < maxPendingDigits ? m_pending + static_cast<char>('0' + digit)
                                                             : std::string(1, static_cast<char>('0' + digit));
    int fret = std::stoi(digits);
    if (fret > edit::maxFret) {
        digits = std::string(1, static_cast<char>('0' + digit));
        fret = digit;
    }
    if (!m_pending.empty() && digits.size() > 1) {
        m_undo.undo(m_project); // the first digit's note makes way for the two-digit fret
    }
    const model::Dur dur = beatAtCursor() != nullptr ? beatAtCursor()->dur : m_inputDur;
    if (run(edit::setNote(score(), refOf(*m_cursor), {m_string, fret}, dur, m_ids))) {
        m_pending = digits;
    }
}

void ScoreEditor::confirm()
{
    m_pending.clear();
    move(Move::Right);
}

void ScoreEditor::toggleRest()
{
    if (!m_cursor) {
        return;
    }
    const model::Beat *beat = beatAtCursor();
    if (beat != nullptr && beat->rest) {
        run(edit::deleteBeat(score(), refOf(*m_cursor)));
        return;
    }
    run(edit::setRest(score(), refOf(*m_cursor), beat != nullptr ? beat->dur : m_inputDur, m_ids));
}

void ScoreEditor::setLength(model::Dur dur)
{
    m_inputDur = dur;
    if (const model::Beat *beat = beatAtCursor(); beat != nullptr) {
        run(edit::setBeatLength(score(), cursorRef(), dur, beat->dots, beat->tuplet));
    }
    emit cursorChanged(); // the input length shows next to the cursor
}

void ScoreEditor::longer()
{
    const model::Beat *beat = beatAtCursor();
    setLength(stepDur(beat != nullptr ? beat->dur : m_inputDur, -1));
}

void ScoreEditor::shorter()
{
    const model::Beat *beat = beatAtCursor();
    setLength(stepDur(beat != nullptr ? beat->dur : m_inputDur, 1));
}

void ScoreEditor::toggleDot()
{
    if (const model::Beat *beat = beatAtCursor(); beat != nullptr) {
        run(edit::setBeatLength(score(), cursorRef(), beat->dur, beat->dots == 0 ? 1 : 0, beat->tuplet));
    }
}

void ScoreEditor::toggleDoubleDot()
{
    if (const model::Beat *beat = beatAtCursor(); beat != nullptr) {
        run(edit::setBeatLength(score(), cursorRef(), beat->dur, beat->dots == 2 ? 0 : 2, beat->tuplet));
    }
}

void ScoreEditor::toggleTuplet()
{
    if (const model::Beat *beat = beatAtCursor(); beat != nullptr) {
        const std::optional<model::Tuplet> tuplet = beat->tuplet ? std::nullopt : std::optional(model::Tuplet{3, 2});
        run(edit::setBeatLength(score(), cursorRef(), beat->dur, beat->dots, tuplet));
    }
}

void ScoreEditor::tieBeat()
{
    const model::Beat *beat = beatAtCursor();
    if (beat == nullptr || beat->notes.empty()) {
        return;
    }
    const bool on = !std::ranges::all_of(beat->notes, &model::Note::tieToNext);
    std::vector<int> strings;
    strings.reserve(beat->notes.size());
    for (const model::Note &note : beat->notes) {
        strings.push_back(note.string);
    }
    for (const int string : strings) {
        run(edit::setTie(score(), cursorRef(), string, on));
    }
}

void ScoreEditor::setVoice(int voice)
{
    if (!m_cursor || voice < 1 || voice == m_cursor->voice) {
        return;
    }
    m_cursor->voice = voice;
    m_pending.clear();
    emit cursorChanged();
}

void ScoreEditor::insertBar()
{
    const std::size_t at = m_cursor ? m_cursor->measure : score().measures.size();
    run(edit::insertMeasures(m_project, at, 1, m_ids));
}

void ScoreEditor::deleteBar()
{
    if (!m_cursor || score().measures.size() <= 1) {
        return;
    }
    BeatLocation at = *m_cursor;
    if (run(edit::deleteMeasures(m_project, at.measure, 1))) {
        at.measure = std::min(at.measure, score().measures.size() - 1);
        at.tick = 0;
        m_cursor = at;
        emit cursorChanged();
    }
}

namespace
{

/// Ticks each (track, voice) fills in the measure.
std::map<std::pair<std::size_t, int>, model::Tick> filledPerVoice(const model::Measure &measure)
{
    std::map<std::pair<std::size_t, int>, model::Tick> filled;
    for (const model::Beat &beat : measure.beats) {
        filled[{beat.track, beat.voice}] += beat.ticks();
    }
    return filled;
}

} // namespace

std::vector<ScoreEditor::BarProblem> ScoreEditor::barProblems() const
{
    std::vector<BarProblem> out;
    for (std::size_t m = 0; m < score().measures.size(); ++m) {
        const model::Tick length = model::measureTicks(score().timeSigAt(m));
        for (const auto &[key, ticks] : filledPerVoice(score().measures[m])) {
            appendProblem(out, m, key, ticks - length);
        }
    }
    return out;
}

void ScoreEditor::appendProblem(std::vector<BarProblem> &out,
                                std::size_t measure,
                                const std::pair<std::size_t, int> &trackVoice,
                                model::Tick delta)
{
    if (delta != 0) {
        out.push_back({measure, trackVoice.first, trackVoice.second, delta});
    }
}

void ScoreEditor::toggleTie()
{
    const model::Beat *beat = beatAtCursor();
    if (beat == nullptr) {
        return;
    }
    const auto note = std::ranges::find(beat->notes, m_string, &model::Note::string);
    if (note != beat->notes.end()) {
        run(edit::setTie(score(), cursorRef(), m_string, !note->tieToNext));
    }
}

void ScoreEditor::deleteNote()
{
    if (m_cursor) {
        run(edit::removeNote(score(), refOf(*m_cursor), m_string));
    }
}

void ScoreEditor::deleteBeat()
{
    if (m_cursor) {
        run(edit::deleteBeat(score(), refOf(*m_cursor)));
    }
}

bool ScoreEditor::runCommand(std::unique_ptr<edit::Command> command)
{
    return run(std::move(command));
}

void ScoreEditor::setActiveSource(std::optional<model::Id> sourceId)
{
    m_project.lastSession.sourceId = std::move(sourceId);
}

void ScoreEditor::undo()
{
    if (m_undo.canUndo()) {
        m_undo.undo(m_project);
        m_pending.clear();
        emit scoreChanged();
    }
}

void ScoreEditor::redo()
{
    if (m_undo.canRedo()) {
        m_undo.redo(m_project);
        m_pending.clear();
        emit scoreChanged();
    }
}

} // namespace cc::ui
