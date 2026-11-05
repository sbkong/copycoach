// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/shell/shell.h"

#include "ui/audio/audioservice.h"
#include "ui/score/scoreeditor.h"
#include "ui/score/scoreview.h"
#include "ui/shell/actionregistry.h"

#include <QLabel>

#include <array>
#include <utility>

// editor.* actions -> ScoreEditor, ScoreEditor -> ScoreView and the engine. The editor context is active
// only while the score view has focus (ActionRegistry::setFocusContext), so digits and arrows reach the
// editor there and stay ordinary keys everywhere else.
namespace cc::ui
{

using action::Id;

namespace
{

constexpr int digitCount = 10;

constexpr std::array<std::pair<Id, ScoreEditor::Move>, 10> moves{{
    {Id::EditorCursorLeft, ScoreEditor::Move::Left},
    {Id::EditorCursorRight, ScoreEditor::Move::Right},
    {Id::EditorCursorUp, ScoreEditor::Move::Up},
    {Id::EditorCursorDown, ScoreEditor::Move::Down},
    {Id::EditorCursorBarStart, ScoreEditor::Move::BarStart},
    {Id::EditorCursorBarEnd, ScoreEditor::Move::BarEnd},
    {Id::EditorCursorFirstBar, ScoreEditor::Move::FirstBar},
    {Id::EditorCursorLastBar, ScoreEditor::Move::LastBar},
    {Id::EditorTrackPrev, ScoreEditor::Move::TrackPrev},
    {Id::EditorTrackNext, ScoreEditor::Move::TrackNext},
}};

} // namespace

void Shell::wireEditorActions()
{
    for (const auto &[id, how] : moves) {
        connect(m_actions->action(id), &QAction::triggered, m_editor, [this, how] {
            m_editor->move(how);
        });
    }
    for (int digit = 0; digit < digitCount; ++digit) {
        connect(m_actions->action(Id::EditorInputDigitN, digit), &QAction::triggered, m_editor, [this, digit] {
            m_editor->typeDigit(digit);
        });
    }
    connect(m_actions->action(Id::EditorInputConfirm), &QAction::triggered, m_editor, &ScoreEditor::confirm);
    connect(m_actions->action(Id::EditorRest), &QAction::triggered, m_editor, &ScoreEditor::toggleRest);
    connect(m_actions->action(Id::EditorDurationLonger), &QAction::triggered, m_editor, &ScoreEditor::longer);
    connect(m_actions->action(Id::EditorDurationShorter), &QAction::triggered, m_editor, &ScoreEditor::shorter);
    connect(m_actions->action(Id::EditorDot), &QAction::triggered, m_editor, &ScoreEditor::toggleDot);
    connect(m_actions->action(Id::EditorTieNote), &QAction::triggered, m_editor, &ScoreEditor::toggleTie);
    connect(m_actions->action(Id::EditorDeleteNote), &QAction::triggered, m_editor, &ScoreEditor::deleteNote);
    connect(m_actions->action(Id::EditorDeleteBeat), &QAction::triggered, m_editor, &ScoreEditor::deleteBeat);
    connect(m_actions->action(Id::EditUndo), &QAction::triggered, this, [this] {
        if (m_active == workspace::Id::Copy) {
            m_editor->undo();
        }
    });
    connect(m_actions->action(Id::EditRedo), &QAction::triggered, this, [this] {
        if (m_active == workspace::Id::Copy) {
            m_editor->redo();
        }
    });
    wireEditorToView();
}

/// The view shows the editor's score and cursor; a click in the view moves the editor's cursor; focus in the
/// view switches the editor context on.
void Shell::wireEditorToView()
{
    connect(m_editor, &ScoreEditor::scoreChanged, this, [this] {
        m_scoreView->updateScore(m_editor->score());
        if (m_active == workspace::Id::Copy) {
            m_audio->setScore(m_editor->score());
        }
    });
    connect(m_editor, &ScoreEditor::cursorChanged, this, [this] {
        m_scoreView->setBeatCursor(m_editor->cursor());
        m_scoreView->setCursorString(m_editor->cursorString());
        updateEditorLabel();
    });
    connect(m_scoreView, &ScoreView::cursorChanged, m_editor, [this] {
        m_editor->setCursor(m_scoreView->beatCursor());
    });
    connect(m_scoreView, &ScoreView::focusChanged, this, [this](bool focused) {
        m_actions->setFocusContext(focused ? workspace::Context::Editor : workspace::Context::Global);
        updateEditorLabel();
    });
}

/// "줄 2 · ♩ · 1_" in the status bar while the editor has focus: the string, the input length, pending digits.
void Shell::updateEditorLabel()
{
    if (!m_scoreView->hasFocus() || !m_editor->cursor()) {
        m_contextLabel->clear();
        return;
    }
    static constexpr std::array<const char *, 7> lengths{"1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64"};
    QString text = tr("줄 %1 · %2")
                       .arg(m_editor->cursorString())
                       .arg(QString::fromLatin1(lengths[static_cast<std::size_t>(m_editor->inputDur())]));
    if (!m_editor->pendingDigits().empty()) {
        text += tr(" · 입력 %1").arg(QString::fromStdString(m_editor->pendingDigits()));
    }
    m_contextLabel->setText(text);
}

} // namespace cc::ui
