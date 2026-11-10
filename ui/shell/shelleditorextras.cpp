// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/shell/shell.h"

#include "core/edit/commands.h"
#include "core/theory/pitch.h"
#include "ui/copy/projectsession.h"
#include "ui/score/scoreeditor.h"
#include "ui/score/scoreview.h"
#include "ui/shell/actionregistry.h"
#include "ui/shell/commandpalette.h"

#include <QRegularExpression>

#include <array>
#include <optional>
#include <string>

// The editor actions the model already carries beyond the C-2 keys (ia.md section 2 "편집", GP8 layout):
// double dot, triplet, tie on the whole beat, voice 1..4 for the cursor, bar insert / delete, delete the
// selection, transpose the selection, the bar length check. Meter, key, tempo, chord symbol and section
// are typed into the command palette ("박자 3/4", "조표 Bb", "템포 120", "코드 Dm7", "섹션 A Head"), which
// keeps the dialog count at three; editor.chord-dialog and editor.section.edit open it with the prefix.
namespace cc::ui
{

using action::Id;

namespace
{

constexpr int voiceCount = 4; // editor.voice.edit-{n} range
constexpr std::array<int, 12> majorFifths{0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5}; // by pitch class

std::optional<model::KeySignature> keyOf(const QString &text)
{
    bool isNumber = false;
    const int fifths = text.trimmed().toInt(&isNumber);
    if (isNumber && fifths >= -7 && fifths <= 7) {
        return model::KeySignature{fifths, false};
    }
    std::string name = text.trimmed().toStdString();
    bool minor = false;
    if (name.size() > 1 && name.back() == 'm') {
        minor = true;
        name.pop_back();
    }
    const auto pitchClass = theory::parsePitchClass(name);
    if (!pitchClass) {
        return std::nullopt;
    }
    const theory::PitchClass major = minor ? theory::transpose(*pitchClass, 3) : *pitchClass;
    return model::KeySignature{majorFifths[static_cast<std::size_t>(major)], minor};
}

std::optional<model::TimeSignature> timeOf(const QString &text)
{
    const QStringList parts = text.split(QLatin1Char('/'));
    if (parts.size() != 2) {
        return std::nullopt;
    }
    const int count = parts[0].trimmed().toInt();
    const int unit = parts[1].trimmed().toInt();
    if (count < 1 || count > 32 || (unit != 1 && unit != 2 && unit != 4 && unit != 8 && unit != 16)) {
        return std::nullopt;
    }
    return model::TimeSignature{count, unit};
}

QString kindLabel(const QString &kind)
{
    if (kind == QStringLiteral("time")) {
        return Shell::tr("박자");
    }
    if (kind == QStringLiteral("key")) {
        return Shell::tr("조표");
    }
    if (kind == QStringLiteral("tempo")) {
        return Shell::tr("템포");
    }
    return kind == QStringLiteral("chord") ? Shell::tr("코드") : Shell::tr("섹션");
}

} // namespace

void Shell::wireEditorExtras()
{
    connect(m_actions->action(Id::EditorDoubleDot), &QAction::triggered, m_editor, &ScoreEditor::toggleDoubleDot);
    connect(m_actions->action(Id::EditorTuplet), &QAction::triggered, m_editor, &ScoreEditor::toggleTuplet);
    connect(m_actions->action(Id::EditorTieBeat), &QAction::triggered, m_editor, &ScoreEditor::tieBeat);
    for (int voice = 1; voice <= voiceCount; ++voice) {
        connect(m_actions->action(Id::EditorVoiceEditN, voice), &QAction::triggered, m_editor, [this, voice] {
            m_editor->setVoice(voice);
        });
        QAction *move = m_actions->action(Id::EditorVoiceMoveN, voice); // no command moves a beat between voices yet
        move->setEnabled(false);
        move->setToolTip(tr("다음 조각에서 구현됩니다"));
    }
    connect(m_actions->action(Id::EditorBarInsert), &QAction::triggered, m_editor, &ScoreEditor::insertBar);
    connect(m_actions->action(Id::EditorBarDelete), &QAction::triggered, m_editor, &ScoreEditor::deleteBar);
    connect(m_actions->action(Id::EditorDeleteSelection), &QAction::triggered, this, &Shell::deleteSelection);
    connect(m_actions->action(Id::EditorTransposeUp), &QAction::triggered, this, [this] {
        transposeSelection(1);
    });
    connect(m_actions->action(Id::EditorTransposeDown), &QAction::triggered, this, [this] {
        transposeSelection(-1);
    });
    connect(m_actions->action(Id::EditorBarCheck), &QAction::triggered, this, &Shell::checkBars);
    connect(m_actions->action(Id::EditorChordDialog), &QAction::triggered, this, [this] {
        m_palette->openWithQuery(tr("코드 "));
    });
    connect(m_actions->action(Id::EditorSectionEdit), &QAction::triggered, this, [this] {
        m_palette->openWithQuery(tr("섹션 "));
    });
    connect(m_palette, &CommandPalette::editorRequested, this, &Shell::applyPaletteRequest);
}

void Shell::deleteSelection()
{
    const auto span = clipboardSpan();
    if (span && m_editor->runCommand(edit::clearMeasures(m_project->project(), *span))) {
        pushCopyScore();
        showStatus(tr("마디 %1~%2 비움").arg(span->from + 1).arg(span->to + 1));
    }
}

void Shell::transposeSelection(int semitones)
{
    const auto span = clipboardSpan();
    if (span && m_editor->runCommand(edit::transposeMeasures(m_project->project(), *span, semitones))) {
        pushCopyScore();
        showStatus(tr("마디 %1~%2 이조 %3").arg(span->from + 1).arg(span->to + 1).arg(semitones > 0 ? "+1" : "-1"));
    }
}

/// editor.bar.check: every (track, voice) whose beats do not fill its measure, as one status line.
void Shell::checkBars()
{
    const std::vector<ScoreEditor::BarProblem> problems = m_editor->barProblems();
    if (problems.empty()) {
        showStatus(tr("마디 길이 검사: 이상 없음"));
        return;
    }
    QStringList parts;
    for (std::size_t i = 0; i < problems.size() && i < 4; ++i) {
        const ScoreEditor::BarProblem &p = problems[i];
        parts << tr("마디 %1 트랙 %2 보이스 %3 %4 %5틱")
                     .arg(p.measure + 1)
                     .arg(p.track + 1)
                     .arg(p.voice)
                     .arg(p.delta > 0 ? tr("넘침") : tr("모자람"))
                     .arg(std::abs(p.delta));
    }
    showStatus(tr("마디 길이 검사: %1곳 — %2").arg(problems.size()).arg(parts.join(QStringLiteral(" · "))));
}

/// The cursor's measure, else the selection's first, else the first measure.
std::size_t Shell::paletteTargetMeasure() const
{
    if (const auto cursor = m_editor->cursor()) {
        return cursor->measure;
    }
    if (const auto selected = m_scoreView->selectedMeasures()) {
        return selected->first;
    }
    return 0;
}

/// A typed palette row: "measure" goes to the measure; the others edit the target measure of the copy score.
void Shell::applyPaletteRequest(const QString &kind, const QString &payload)
{
    if (kind == QStringLiteral("measure")) {
        goToMeasure(payload.toInt());
        return;
    }
    if (m_active != workspace::Id::Copy || !m_scoreView->hasScore() || m_scoreView->score().measures.empty()) {
        showStatus(tr("카피 작업공간의 악보에서만 됩니다"));
        return;
    }
    const std::size_t measure = paletteTargetMeasure();
    const bool changed = applyPaletteEdit(kind, payload, measure);
    if (changed) {
        pushCopyScore();
        showStatus(tr("마디 %1: %2 %3").arg(measure + 1).arg(kindLabel(kind)).arg(payload));
    } else {
        showStatus(tr("알아듣지 못했습니다: %1").arg(payload));
    }
}

bool Shell::applyPaletteEdit(const QString &kind, const QString &payload, std::size_t measure)
{
    const model::Score &score = m_editor->score();
    if (kind == QStringLiteral("time")) {
        const auto time = timeOf(payload);
        return time && m_editor->runCommand(edit::setTimeSig(score, measure, *time));
    }
    if (kind == QStringLiteral("key")) {
        const auto key = keyOf(payload);
        return key && m_editor->runCommand(edit::setKeySig(score, measure, *key));
    }
    if (kind == QStringLiteral("tempo")) {
        const double bpm = payload.toDouble();
        return bpm >= 20.0 && bpm <= 400.0 && m_editor->runCommand(edit::setTempo(score, measure, bpm));
    }
    if (kind == QStringLiteral("chord")) {
        const model::Tick tick = m_editor->cursor() ? m_editor->cursor()->tick : 0;
        return m_editor->runCommand(edit::setChordSymbol(score, measure, tick, payload.toStdString(), m_rangeIds));
    }
    if (kind == QStringLiteral("section")) {
        const QStringList parts = payload.split(QLatin1Char('|'));
        return m_editor->runCommand(
            edit::setSection(score, measure, parts.value(0).toStdString(), parts.value(1).toStdString(), m_rangeIds));
    }
    return false;
}

} // namespace cc::ui
