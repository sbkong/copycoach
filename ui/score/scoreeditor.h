// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/edit/commands.h"
#include "core/edit/undostack.h"
#include "core/model/id.h"
#include "core/model/score.h"
#include "core/project/project.h"
#include "ui/score/overlays.h"

#include <QObject>

#include <memory>
#include <optional>
#include <string>

namespace cc::ui
{

/// The keyboard editor behind the score view (editor.* actions, score-data-path.md section 2): a cursor on a
/// beat slot and a string, fret digits that become setNote commands, rests, lengths, dots, ties, deletes, and
/// undo / redo over the project's UndoStack. The score the view shows is this editor's. Main thread only.
class ScoreEditor final : public QObject
{
    Q_OBJECT
public:
    enum class Move : std::uint8_t {
        Left,
        Right,
        Up, // string above (toward string 1)
        Down,
        BarStart,
        BarEnd,
        FirstBar,
        LastBar,
        TrackPrev,
        TrackNext,
    };

    explicit ScoreEditor(QObject *parent = nullptr);

    /// Wraps the score in a fresh project; the undo history starts over, the cursor goes to the first beat.
    void setScore(const model::Score &score);
    /// A project from disk (or a new one): history starts over, the cursor goes to the first beat.
    void setProject(project::Project project);
    void clearProject(); // back to "nothing open"
    [[nodiscard]] bool hasProject() const;
    /// The score replaced as an undoable edit of the open project (an import into it).
    void replaceScore(const model::Score &score);
    [[nodiscard]] const model::Score &score() const;
    [[nodiscard]] const project::Project &project() const;
    /// last_session <-> cursor (copy-spec.md section 1: the playhead is a tick).
    void storeLastSession();
    void restoreLastSession();
    [[nodiscard]] edit::UndoStack &undoStack();

    void setCursor(std::optional<BeatLocation> cursor);
    [[nodiscard]] std::optional<BeatLocation> cursor() const;
    [[nodiscard]] int cursorString() const;
    [[nodiscard]] model::Dur inputDur() const; // length a typed note gets when it opens a new beat
    [[nodiscard]] const std::string &pendingDigits() const;

    void move(Move how);

    // editor.input.digit-{n} / confirm / rest / duration.* / dot / tie.note / delete.*
    void typeDigit(int digit);
    void confirm(); // ends the digit entry and steps right
    void toggleRest();
    void longer();
    void shorter();
    void toggleDot();
    void toggleTie();
    void deleteNote();
    void deleteBeat();

    void undo();
    void redo();

signals:
    void scoreChanged();
    void cursorChanged();

private:
    [[nodiscard]] const model::Beat *beatAtCursor() const;
    [[nodiscard]] edit::BeatRef cursorRef() const;
    [[nodiscard]] std::optional<BeatLocation> stepRight(const BeatLocation &from) const;
    [[nodiscard]] std::optional<BeatLocation> stepLeft(const BeatLocation &from) const;
    [[nodiscard]] BeatLocation snapToTrack(const BeatLocation &from, std::size_t track) const;
    [[nodiscard]] BeatLocation barEdge(const BeatLocation &from, std::size_t measure, bool end) const;
    void setStringClamped(int string);
    bool run(std::unique_ptr<edit::Command> command); // push, announce; false when nothing changed
    void setLength(model::Dur dur);

    project::Project m_project;
    edit::UndoStack m_undo;
    model::IdGenerator m_ids;
    std::optional<BeatLocation> m_cursor;
    int m_string = 1;
    model::Dur m_inputDur = model::Dur::Quarter;
    std::string m_pending; // digits typed since the last confirm / move
    bool m_hasProject = false;
};

} // namespace cc::ui
