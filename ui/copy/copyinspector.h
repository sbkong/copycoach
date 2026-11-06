// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/model/id.h"

#include <QWidget>

#include <cstddef>
#include <optional>
#include <utility>

class QButtonGroup;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QTimer;

namespace cc::ui
{

class ScoreEditor;
class Theme;

/// The copy workspace's inspector (copy-spec.md section 16.2 C-5, memo anchors section 7-6): the song group
/// with the song memo and the lyrics, the range group with the saved ranges and the memo of the selected
/// one. Text lands in the project as commands after a short pause in typing, one command per pause, so
/// undo steps through what was written rather than every keystroke.
class CopyInspector final : public QWidget
{
    Q_OBJECT
public:
    static constexpr int commitDelayMs = 600;

    CopyInspector(const Theme &theme, ScoreEditor &editor, QWidget *parent = nullptr);

    /// Re-reads the project (after a load, an undo, a range edit); typing in progress is not overwritten.
    void refresh();
    void setTitle(const QString &title);
    /// The measures selected in the score, offered as "save as range".
    void setSelectedMeasures(std::optional<std::pair<std::size_t, std::size_t>> range);

    [[nodiscard]] int stage() const; // the checked stage button, 1..7
    [[nodiscard]] std::optional<model::Id> selectedRange() const;
    void selectRange(const std::optional<model::Id> &id);
    void commitNow(); // flush pending text (tests, before a save)

signals:
    void rangeChosen(const model::Id &id); // the user picked a range in the list
    void stageChosen(int stage); // the user clicked a stage button

private:
    void buildStageRow(const Theme &theme);
    void buildSongGroup(const Theme &theme);
    void buildRangeGroup(const Theme &theme);
    void refreshRanges();
    void rangeRowChanged(int row);
    void commitSong();
    void commitRangeMemo();

    ScoreEditor *m_editor;
    QLabel *m_title;
    QButtonGroup *m_stages;
    QLabel *m_stageHint;
    QPlainTextEdit *m_memo;
    QPlainTextEdit *m_lyrics;
    QLabel *m_selectionLabel;
    QListWidget *m_ranges;
    QPlainTextEdit *m_rangeMemo;
    QTimer *m_commit;
    std::optional<std::pair<std::size_t, std::size_t>> m_selected;
    bool m_refreshing = false;
};

} // namespace cc::ui
