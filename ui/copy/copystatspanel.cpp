// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/copy/copystatspanel.h"

#include "ui/copy/copystatsmodel.h"
#include "ui/theme/theme.h"

#include <QHeaderView>
#include <QLabel>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cstdint>

namespace cc::ui
{

namespace
{

constexpr int secondsPerMinute = 60;
constexpr int minutesPerHour = 60;

enum class Column : std::uint8_t {
    Name,
    Plays,
    Time,
    Repeats,
    TopSpeed,
    Count,
};

constexpr int col(Column column)
{
    return static_cast<int>(column);
}

} // namespace

QString practiceTimeText(int seconds)
{
    const int minutes = seconds / secondsPerMinute;
    if (minutes == 0) {
        return QObject::tr("%1초").arg(seconds);
    }
    if (minutes < minutesPerHour) {
        return QObject::tr("%1분").arg(minutes);
    }
    return QObject::tr("%1시간 %2분").arg(minutes / minutesPerHour).arg(minutes % minutesPerHour);
}

CopyStatsPanel::CopyStatsPanel(const Theme &theme, const CopyStatsModel &model, QWidget *parent)
    : QWidget(parent)
    , m_theme(&theme)
    , m_model(&model)
    , m_empty(new QLabel(this))
    , m_content(new QWidget(this))
    , m_time(new QLabel(m_content))
    , m_table(new QTreeWidget(m_content))
{
    auto *layout = new QVBoxLayout(this);
    const int edge = theme.spacing(theme::Spacing::ContentArea);
    layout->setContentsMargins(edge, edge, edge, edge);
    layout->setSpacing(theme.spacing(theme::Spacing::Control));
    m_empty->setWordWrap(true);
    m_empty->setAccessibleName(tr("기록 빈 상태"));
    layout->addWidget(m_empty);
    auto *content = new QVBoxLayout(m_content);
    content->setContentsMargins(0, 0, 0, 0);
    content->setSpacing(theme.spacing(theme::Spacing::Control));
    m_time->setFont(theme.font(theme::TypeStyle::BodyStrong, true));
    m_time->setAccessibleName(tr("이 곡 연습 시간"));
    content->addWidget(m_time);
    m_table->setColumnCount(col(Column::Count));
    m_table->setHeaderLabels({tr("구간"), tr("재생"), tr("시간"), tr("반복"), tr("최고 속도")});
    m_table->setRootIsDecorated(false);
    m_table->setUniformRowHeights(true);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->header()->setSectionResizeMode(col(Column::Name), QHeaderView::Stretch);
    m_table->header()->setStretchLastSection(false);
    m_table->setAccessibleName(tr("구간별 기록"));
    content->addWidget(m_table, 1);
    layout->addWidget(m_content, 1);
    connect(m_model, &CopyStatsModel::changed, this, &CopyStatsPanel::refresh);
    refresh();
}

QTreeWidget *CopyStatsPanel::table() const
{
    return m_table;
}

/// No project: nothing to count. A project with no plays yet: the first play fills the table.
void CopyStatsPanel::refresh()
{
    const bool show = m_model->hasProject() && m_model->hasRecords();
    m_empty->setText(m_model->hasProject() ? tr("아직 이 곡의 기록이 없습니다. 원곡을 재생하면 자동으로 쌓입니다.")
                                           : tr("프로젝트를 열면 이 곡의 기록이 보입니다."));
    m_empty->setVisible(!show);
    m_content->setVisible(show);
    if (!show) {
        return;
    }
    m_time->setText(tr("오늘 %1 · 전체 %2")
                        .arg(practiceTimeText(m_model->todaySeconds()), practiceTimeText(m_model->totalSeconds())));
    m_table->clear();
    for (const CopyStatsModel::LoopRow &row : m_model->rows()) {
        auto *item = new QTreeWidgetItem(m_table);
        const bool played = row.stats.plays > 0;
        item->setText(col(Column::Name), row.name);
        item->setText(col(Column::Plays), QString::number(row.stats.plays));
        item->setText(col(Column::Time), played ? practiceTimeText(row.stats.seconds) : QStringLiteral("–"));
        item->setText(col(Column::Repeats), QString::number(row.stats.repeats));
        item->setText(col(Column::TopSpeed), played ? tr("%1 %").arg(row.stats.maxSpeedPct) : QStringLiteral("–"));
        for (int column = col(Column::Plays); column < col(Column::Count); ++column) {
            item->setTextAlignment(column, Qt::AlignRight | Qt::AlignVCenter);
        }
    }
}

} // namespace cc::ui
