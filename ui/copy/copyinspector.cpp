// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/copy/copyinspector.h"

#include "core/edit/commands.h"
#include "ui/score/scoreeditor.h"
#include "ui/theme/theme.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

#include <array>

namespace cc::ui
{

namespace
{

/// copy-spec.md section 2: the seven stages, 1-based like the registry's {n} and the stage.enter event.
constexpr std::array<const char *, 7> stageNames{"재료", "멜로디", "코드·폼", "프레이즈", "분석", "어휘", "채보"};
constexpr std::array<const char *, 7> stageHints{
    "음원·영상·악보를 모으고 키·박자·템포를 확인한다",
    "멜로디를 듣고 노래하고 적는다",
    "코드 기호와 섹션을 채운다",
    "구간을 저장하고 감속 루프로 익힌다",
    "구간 안 음을 현재 코드 기준 도수로 본다",
    "프레이즈를 저장하고 다른 키로 본다",
    "MusicXML로 내보낸다",
};

constexpr int memoRows = 4;
constexpr int lyricsRows = 6;

QLabel *heading(const Theme &theme, const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setFont(theme.font(theme::TypeStyle::BodyStrong));
    return label;
}

QPlainTextEdit *textBox(const Theme &theme, const QString &placeholder, int rows, QWidget *parent)
{
    auto *edit = new QPlainTextEdit(parent);
    edit->setFont(theme.font(theme::TypeStyle::Body));
    edit->setPlaceholderText(placeholder);
    edit->setAccessibleName(placeholder);
    edit->setFixedHeight((edit->fontMetrics().lineSpacing() * rows) + (theme.spacing(theme::Spacing::Control) * 2));
    return edit;
}

} // namespace

CopyInspector::CopyInspector(const Theme &theme, ScoreEditor &editor, QWidget *parent)
    : QWidget(parent)
    , m_editor(&editor)
    , m_title(new QLabel(this))
    , m_stages(new QButtonGroup(this))
    , m_stageHint(new QLabel(this))
    , m_memo(textBox(theme, tr("곡 메모"), memoRows, this))
    , m_lyrics(textBox(theme, tr("가사"), lyricsRows, this))
    , m_selectionLabel(new QLabel(this))
    , m_ranges(new QListWidget(this))
    , m_rangeMemo(textBox(theme, tr("구간 메모"), memoRows, this))
    , m_commit(new QTimer(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(theme.spacing(theme::Spacing::ControlLabel));
    m_title->setFont(theme.font(theme::TypeStyle::BodyStrong));
    layout->addWidget(m_title);
    buildStageRow(theme);
    buildSongGroup(theme);
    buildRangeGroup(theme);
    layout->addStretch(1);
    m_commit->setSingleShot(true);
    m_commit->setInterval(commitDelayMs);
    connect(m_commit, &QTimer::timeout, this, &CopyInspector::commitNow);
    connect(m_editor, &ScoreEditor::scoreChanged, this, &CopyInspector::refresh);
    refresh();
}

/// Seven toggle buttons in a row; the checked one is the project's stage (copy-spec.md section 2).
void CopyInspector::buildStageRow(const Theme &theme)
{
    auto *layout = qobject_cast<QVBoxLayout *>(this->layout());
    layout->addWidget(heading(theme, tr("단계"), this));
    auto *row = new QHBoxLayout();
    row->setSpacing(theme.spacing(theme::Spacing::ControlLabel));
    m_stages->setExclusive(true);
    for (std::size_t i = 0; i < stageNames.size(); ++i) {
        auto *button = new QToolButton(this);
        button->setText(QString::number(i + 1));
        button->setToolTip(tr("%1 %2").arg(i + 1).arg(QString::fromUtf8(stageNames[i])));
        button->setAccessibleName(tr("단계 %1 %2").arg(i + 1).arg(QString::fromUtf8(stageNames[i])));
        button->setCheckable(true);
        button->setAutoRaise(true);
        m_stages->addButton(button, static_cast<int>(i) + 1);
        row->addWidget(button);
    }
    row->addStretch(1);
    layout->addLayout(row);
    m_stageHint->setFont(theme.font(theme::TypeStyle::Caption));
    m_stageHint->setWordWrap(true);
    m_stageHint->setAccessibleName(tr("단계 설명"));
    layout->addWidget(m_stageHint);
    connect(m_stages, &QButtonGroup::idClicked, this, [this](int stage) {
        if (!m_refreshing) {
            emit stageChosen(stage);
        }
    });
    m_stages->button(1)->setChecked(true);
    m_stageHint->setText(tr("%1 · %2").arg(QString::fromUtf8(stageNames[0]), QString::fromUtf8(stageHints[0])));
}

int CopyInspector::stage() const
{
    return std::max(1, m_stages->checkedId());
}

void CopyInspector::buildSongGroup(const Theme &theme)
{
    auto *layout = qobject_cast<QVBoxLayout *>(this->layout());
    layout->addWidget(heading(theme, tr("곡"), this));
    layout->addWidget(m_memo);
    layout->addWidget(m_lyrics);
    const auto restart = [this] {
        if (!m_refreshing) {
            m_commit->start();
        }
    };
    connect(m_memo, &QPlainTextEdit::textChanged, this, restart);
    connect(m_lyrics, &QPlainTextEdit::textChanged, this, restart);
}

void CopyInspector::buildRangeGroup(const Theme &theme)
{
    auto *layout = qobject_cast<QVBoxLayout *>(this->layout());
    layout->addWidget(heading(theme, tr("구간"), this));
    m_selectionLabel->setFont(theme.font(theme::TypeStyle::Caption));
    layout->addWidget(m_selectionLabel);
    m_ranges->setFont(theme.font(theme::TypeStyle::Body));
    m_ranges->setAccessibleName(tr("구간 목록"));
    layout->addWidget(m_ranges);
    layout->addWidget(m_rangeMemo);
    connect(m_ranges, &QListWidget::currentRowChanged, this, &CopyInspector::rangeRowChanged);
    connect(m_rangeMemo, &QPlainTextEdit::textChanged, this, [this] {
        if (!m_refreshing) {
            m_commit->start();
        }
    });
    setSelectedMeasures(std::nullopt);
}

/// A row picked by the user (not by refresh or selectRange) is announced.
void CopyInspector::rangeRowChanged(int row)
{
    refreshRanges();
    const auto id = selectedRange();
    if (row >= 0 && !m_refreshing && id) {
        emit rangeChosen(*id);
    }
}

void CopyInspector::setTitle(const QString &title)
{
    m_title->setText(title.isEmpty() ? tr("프로젝트 없음") : title);
}

void CopyInspector::setSelectedMeasures(std::optional<std::pair<std::size_t, std::size_t>> range)
{
    m_selected = range;
    if (range) {
        m_selectionLabel->setText(
            tr("선택 마디 %1~%2 · 구간 추가로 저장").arg(range->first + 1).arg(range->second + 1));
    } else {
        m_selectionLabel->setText(tr("악보에서 마디를 선택하면 구간으로 저장할 수 있습니다"));
    }
}

std::optional<model::Id> CopyInspector::selectedRange() const
{
    const QListWidgetItem *item = m_ranges->currentItem();
    if (item == nullptr) {
        return std::nullopt;
    }
    return item->data(Qt::UserRole).toString().toStdString();
}

void CopyInspector::selectRange(const std::optional<model::Id> &id)
{
    const bool was = m_refreshing;
    m_refreshing = true; // programmatic: the list follows, nobody is told
    int target = -1;
    for (int row = 0; row < m_ranges->count(); ++row) {
        if (id && m_ranges->item(row)->data(Qt::UserRole).toString().toStdString() == *id) {
            target = row;
        }
    }
    m_ranges->setCurrentRow(target);
    m_refreshing = was;
}

/// The widgets take the project's text unless the user is typing there (a pending commit).
void CopyInspector::refresh()
{
    if (!m_editor->hasProject()) {
        return;
    }
    m_refreshing = true;
    const project::Project &project = m_editor->project();
    if (QAbstractButton *button = m_stages->button(project.stage); button != nullptr) {
        button->setChecked(true);
        const auto index = static_cast<std::size_t>(project.stage - 1);
        m_stageHint->setText(
            tr("%1 · %2").arg(QString::fromUtf8(stageNames[index]), QString::fromUtf8(stageHints[index])));
    }
    if (!m_commit->isActive()) {
        const QString memo = QString::fromStdString(project.memos.project);
        if (m_memo->toPlainText() != memo) {
            m_memo->setPlainText(memo);
        }
        const QString lyrics = QString::fromStdString(project.lyrics);
        if (m_lyrics->toPlainText() != lyrics) {
            m_lyrics->setPlainText(lyrics);
        }
    }
    const std::optional<model::Id> keep = selectedRange();
    m_ranges->clear();
    for (const project::Range &range : project.ranges) {
        auto *item = new QListWidgetItem(
            tr("%1 (마디 %2~%3)").arg(QString::fromStdString(range.name)).arg(range.barFrom + 1).arg(range.barTo + 1),
            m_ranges);
        item->setData(Qt::UserRole, QString::fromStdString(range.id));
    }
    selectRange(keep);
    refreshRanges();
    m_refreshing = false;
}

/// The range memo follows the list selection.
void CopyInspector::refreshRanges()
{
    const auto id = selectedRange();
    m_rangeMemo->setEnabled(id.has_value());
    if (!id || m_commit->isActive()) {
        if (!id) {
            const bool was = m_refreshing;
            m_refreshing = true;
            m_rangeMemo->clear();
            m_refreshing = was;
        }
        return;
    }
    const auto &memos = m_editor->project().memos.ranges;
    const auto it = memos.find(*id);
    const QString text = it == memos.end() ? QString() : QString::fromStdString(it->second);
    if (m_rangeMemo->toPlainText() != text) {
        const bool was = m_refreshing;
        m_refreshing = true;
        m_rangeMemo->setPlainText(text);
        m_refreshing = was;
    }
}

void CopyInspector::commitNow()
{
    m_commit->stop();
    commitSong();
    commitRangeMemo();
}

void CopyInspector::commitSong()
{
    if (!m_editor->hasProject()) {
        return;
    }
    const project::Project &project = m_editor->project();
    m_editor->runCommand(edit::setProjectMemo(project, m_memo->toPlainText().toStdString()));
    m_editor->runCommand(edit::setLyrics(m_editor->project(), m_lyrics->toPlainText().toStdString()));
}

void CopyInspector::commitRangeMemo()
{
    const auto id = selectedRange();
    if (!id || !m_editor->hasProject()) {
        return;
    }
    m_editor->runCommand(edit::setRangeMemo(m_editor->project(), *id, m_rangeMemo->toPlainText().toStdString()));
}

} // namespace cc::ui
