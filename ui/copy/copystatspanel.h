// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <QWidget>

class QLabel;
class QTreeWidget;

// Stats dock, copy context (stats.md section 3, components.md section 2 "기록 도크": a standard table for the
// copy side): this song's playing time today and in all, and a loop table (range, plays, time, wraps, top
// speed). Empty state text from stats.md section 7.
namespace cc::ui
{

class CopyStatsModel;
class Theme;

class CopyStatsPanel final : public QWidget
{
    Q_OBJECT
public:
    CopyStatsPanel(const Theme &theme, const CopyStatsModel &model, QWidget *parent = nullptr);

    [[nodiscard]] QTreeWidget *table() const; // tests read the rows

private:
    void refresh();

    const Theme *m_theme;
    const CopyStatsModel *m_model;
    QLabel *m_empty;
    QWidget *m_content;
    QLabel *m_time;
    QLabel *m_stages;
    QTreeWidget *m_table;
};

/// "1시간 5분" / "5분" / "40초" for the dock's time cells.
[[nodiscard]] QString practiceTimeText(int seconds);

} // namespace cc::ui
