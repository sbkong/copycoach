// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/routine/statspanel.h"

#include "core/theory/pitch.h"
#include "ui/routine/routinetimer.h"
#include "ui/routine/statsmodel.h"
#include "ui/theme/theme.h"

#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>

namespace cc::ui
{

namespace
{

using theme::Token;

constexpr int secondsPerMinute = 60;
constexpr int keyCount = 12;
constexpr int minCell = 12;
constexpr int maxCell = 28;
constexpr int alphaSteps = 4; // stats.md section 6: accent alpha in four steps
constexpr double barHeightShare = 0.55; // of the row height
constexpr double labelShareMax = 0.4; // of the row width: a long module name elides so the bar keeps room
constexpr int minTrackWidth = 48; // under this the value text goes, never the bar

QString fromView(std::string_view view)
{
    return QString::fromUtf8(view.data(), static_cast<qsizetype>(view.size()));
}

QLabel *groupLabel(const QString &text, const Theme &theme, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setFont(theme.font(theme::TypeStyle::Caption));
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, theme.color(Token::ContentColorSecondary));
    label->setPalette(palette);
    return label;
}

std::string moduleIdOf(int module)
{
    return QStringLiteral("0%1").arg(module + 1).toStdString();
}

} // namespace

// ---- ModuleBarsView ----

ModuleBarsView::ModuleBarsView(const Theme &theme,
                               const StatsModel &model,
                               const RoutineTimerController &timer,
                               QWidget *parent)
    : QWidget(parent)
    , m_theme(&theme)
    , m_model(&model)
    , m_timer(&timer)
{
    setAccessibleName(tr("오늘 모듈별 분"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

int ModuleBarsView::rowHeight() const
{
    return QFontMetrics(m_theme->font(theme::TypeStyle::Caption)).height() + m_theme->spacing(theme::Spacing::Unit);
}

QSize ModuleBarsView::sizeHint() const
{
    return {0, rowHeight() * StatsModel::moduleCount};
}

void ModuleBarsView::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setFont(m_theme->font(theme::TypeStyle::Caption, true));
    const Columns columns = columnsFor(QFontMetrics(painter.font()), width());
    const int height = rowHeight();
    for (int module = 0; module < StatsModel::moduleCount; ++module) {
        paintRow(painter, module, QRect(0, module * height, width(), height), columns);
    }
}

QString ModuleBarsView::valueText(int module) const
{
    return tr("%1 / %2분")
        .arg(m_model->todaySeconds(module) / secondsPerMinute)
        .arg(m_timer->lengthSeconds(module) / secondsPerMinute);
}

/// The label column takes the longest module name up to labelShareMax of the row; the value column is
/// measured on the texts actually shown and dropped when the track would fall under minTrackWidth.
ModuleBarsView::Columns ModuleBarsView::columnsFor(const QFontMetrics &metrics, int rowWidth) const
{
    const int gap = m_theme->spacing(theme::Spacing::Unit);
    Columns columns;
    int longestValue = 0;
    for (int module = 0; module < StatsModel::moduleCount; ++module) {
        columns.label = std::max(columns.label, metrics.horizontalAdvance(m_timer->moduleLabel(module)));
        longestValue = std::max(longestValue, metrics.horizontalAdvance(valueText(module)));
    }
    columns.label = std::min(columns.label, static_cast<int>(rowWidth * labelShareMax));
    if (rowWidth - columns.label - longestValue - (2 * gap) >= minTrackWidth) {
        columns.value = longestValue;
    }
    return columns;
}

void ModuleBarsView::paintRow(QPainter &painter, int module, const QRect &row, const Columns &columns) const
{
    const int gap = m_theme->spacing(theme::Spacing::Unit);
    const int seconds = m_model->todaySeconds(module);
    const int target = m_timer->lengthSeconds(module);
    const bool done = target > 0 && seconds >= target;
    const QFontMetrics metrics(painter.font());
    painter.setPen(m_theme->color(Token::ContentColorDefault));
    painter.drawText(QRect(row.left(), row.top(), columns.label, row.height()),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     metrics.elidedText(m_timer->moduleLabel(module), Qt::ElideRight, columns.label));
    const int barLeft = row.left() + columns.label + gap;
    const int barRight = columns.value > 0 ? row.right() - columns.value - gap : row.right();
    const int barHeight = static_cast<int>(row.height() * barHeightShare);
    const QRect track(barLeft, row.center().y() - (barHeight / 2), std::max(0, barRight - barLeft), barHeight);
    painter.setPen(m_theme->color(Token::BorderColorDefault));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(track);
    const double share = target > 0 ? std::min(1.0, static_cast<double>(seconds) / target) : 0.0;
    painter.fillRect(QRect(track.left(), track.top(), static_cast<int>(track.width() * share), track.height()),
                     m_theme->color(Token::AccentColorDefault));
    if (columns.value > 0) {
        painter.setPen(m_theme->color(done ? Token::ContentColorDefault : Token::ContentColorSecondary));
        painter.drawText(QRect(row.right() - columns.value, row.top(), columns.value, row.height()),
                         Qt::AlignVCenter | Qt::AlignRight,
                         valueText(module));
    }
}

// ---- CoverageHeatmapView ----

CoverageHeatmapView::CoverageHeatmapView(const Theme &theme, const StatsModel &model, QWidget *parent)
    : QWidget(parent)
    , m_theme(&theme)
    , m_model(&model)
{
    setAccessibleName(tr("키 × 모듈 커버리지 (최근 28일)"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

int CoverageHeatmapView::count(int module, int pitchClass) const
{
    const stats::KeyCoverage &coverage = m_model->coverage();
    const auto perModule = coverage.find(moduleIdOf(module));
    if (perModule == coverage.end()) {
        return 0;
    }
    const auto perKey = perModule->second.find(std::string(theory::flatName(pitchClass)));
    return perKey == perModule->second.end() ? 0 : perKey->second;
}

int CoverageHeatmapView::maxCount() const
{
    int largest = 0;
    for (const auto &[module, perKey] : m_model->coverage()) {
        for (const auto &[key, n] : perKey) {
            largest = std::max(largest, n);
        }
    }
    return largest;
}

int CoverageHeatmapView::level(int module, int pitchClass) const
{
    const int n = count(module, pitchClass);
    const int largest = maxCount();
    if (n <= 0 || largest <= 0) {
        return 0;
    }
    return std::clamp(((n * alphaSteps) + largest - 1) / largest, 1, alphaSteps); // ceil(n * 4 / largest)
}

int CoverageHeatmapView::labelWidth() const
{
    const QFontMetrics metrics(m_theme->font(theme::TypeStyle::Caption, true));
    return metrics.horizontalAdvance(QStringLiteral("00")) + m_theme->spacing(theme::Spacing::Unit);
}

int CoverageHeatmapView::headerHeight() const
{
    return QFontMetrics(m_theme->font(theme::TypeStyle::Caption)).height() + m_theme->spacing(theme::Spacing::Unit);
}

int CoverageHeatmapView::cellSize() const
{
    return std::clamp((width() - labelWidth()) / keyCount, minCell, maxCell);
}

QSize CoverageHeatmapView::sizeHint() const
{
    return {labelWidth() + (keyCount * minCell), headerHeight() + (StatsModel::moduleCount * cellSize())};
}

void CoverageHeatmapView::resizeEvent(QResizeEvent * /*event*/)
{
    setMinimumHeight(headerHeight() + (StatsModel::moduleCount * cellSize()));
}

void CoverageHeatmapView::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setFont(m_theme->font(theme::TypeStyle::Caption, true));
    paintHeader(painter);
    paintCells(painter);
}

void CoverageHeatmapView::paintHeader(QPainter &painter) const
{
    const int cell = cellSize();
    const int left = labelWidth();
    painter.setPen(m_theme->color(Token::ContentColorSecondary));
    for (int pc = 0; pc < keyCount; ++pc) {
        painter.drawText(QRect(left + (pc * cell), 0, cell, headerHeight()),
                         Qt::AlignCenter,
                         fromView(theory::flatName(pc)));
    }
    for (int module = 0; module < StatsModel::moduleCount; ++module) {
        painter.drawText(QRect(0, headerHeight() + (module * cell), left, cell),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         QString::fromStdString(moduleIdOf(module)));
    }
}

void CoverageHeatmapView::paintCells(QPainter &painter) const
{
    const int cell = cellSize();
    const int left = labelWidth();
    const int top = headerHeight();
    const QColor accent = m_theme->color(Token::AccentColorDefault);
    painter.setPen(m_theme->color(Token::BorderColorDefault));
    for (int module = 0; module < StatsModel::moduleCount; ++module) {
        for (int pc = 0; pc < keyCount; ++pc) {
            const QRect box(left + (pc * cell), top + (module * cell), cell - 1, cell - 1);
            QColor fill = accent;
            fill.setAlphaF(static_cast<float>(level(module, pc)) / alphaSteps); // step 0 = transparent
            painter.fillRect(box, fill);
            painter.drawRect(box);
        }
    }
}

// ---- StatsPanel ----

StatsPanel::StatsPanel(const Theme &theme,
                       const StatsModel &model,
                       const RoutineTimerController &timer,
                       QWidget *parent)
    : QWidget(parent)
    , m_theme(&theme)
    , m_model(&model)
    , m_empty(new QLabel(tr("아직 기록이 없습니다. 루틴 타이머를 시작하면 자동으로 쌓입니다."), this))
    , m_content(new QWidget(this))
    , m_streak(new QLabel(m_content))
    , m_heatmap(new CoverageHeatmapView(theme, model, m_content))
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
    content->addWidget(groupLabel(tr("오늘"), theme, m_content));
    content->addWidget(new ModuleBarsView(theme, model, timer, m_content));
    m_streak->setFont(theme.font(theme::TypeStyle::BodyStrong, true));
    m_streak->setAccessibleName(tr("연속 일수"));
    content->addWidget(m_streak);
    content->addWidget(groupLabel(tr("키 × 모듈 (최근 28일)"), theme, m_content));
    content->addWidget(m_heatmap);
    layout->addWidget(m_content);
    layout->addStretch();
    applyTheme();
    sync();
    connect(m_model, &StatsModel::changed, this, &StatsPanel::sync);
    connect(m_theme, &Theme::changed, this, &StatsPanel::applyTheme);
}

bool StatsPanel::isEmptyStateShown() const
{
    return !m_empty->isHidden();
}

CoverageHeatmapView *StatsPanel::heatmap() const
{
    return m_heatmap;
}

void StatsPanel::sync()
{
    const bool empty = !m_model->hasRecords();
    m_empty->setVisible(empty);
    m_content->setVisible(!empty);
    m_streak->setText(tr("연속 %1일").arg(m_model->streakDays()));
    for (QWidget *chart : m_content->findChildren<QWidget *>()) {
        chart->update();
    }
}

void StatsPanel::applyTheme()
{
    QPalette palette = m_empty->palette();
    palette.setColor(QPalette::WindowText, m_theme->color(Token::ContentColorSecondary));
    m_empty->setPalette(palette);
    for (QWidget *chart : m_content->findChildren<QWidget *>()) {
        chart->update();
    }
}

} // namespace cc::ui
