// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/prefs/preferencesdialog.h"

#include "ui/prefs/appearancepage.h"
#include "ui/prefs/audiodevicepage.h"
#include "ui/prefs/practicepage.h"
#include "ui/prefs/shortcutspage.h"
#include "ui/theme/theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace cc::ui
{

namespace
{

constexpr int dialogWidth = 900; // mockup M8
constexpr int dialogHeight = 600;
constexpr int listWidth = 160;

/// A page that only says it is not written yet (ia.md section 7 lists nine; six come with later pieces).
class PlaceholderPage final : public PreferencesPageBase
{
public:
    PlaceholderPage(const QString &title, const Theme &theme, QWidget *parent)
        : PreferencesPageBase(parent)
    {
        auto *layout = new QVBoxLayout(this);
        auto *heading = new QLabel(title, this);
        heading->setFont(theme.font(theme::TypeStyle::Subtitle));
        layout->addWidget(heading);
        layout->addWidget(new QLabel(tr("이 페이지는 다음 조각에서 채워집니다."), this));
        layout->addStretch();
    }

    void restoreDefaults() override
    {
    }
};

} // namespace

bool PreferencesPageBase::needsApply() const
{
    return false;
}

void PreferencesPageBase::apply()
{
}

void PreferencesPageBase::discard()
{
}

PreferencesDialog::PreferencesDialog(const PreferencesContext &context, QWidget *parent)
    : QDialog(parent)
    , m_list(new QListWidget(this))
    , m_stack(new QStackedWidget(this))
    , m_restore(new QPushButton(tr("기본값 복원"), this))
    , m_cancel(new QPushButton(tr("취소"), this))
    , m_apply(new QPushButton(tr("적용"), this))
{
    setWindowTitle(tr("환경설정"));
    resize(dialogWidth, dialogHeight);
    buildLayout();
    const Theme &theme = *context.theme;
    addPage(PreferencesPage::AudioDevice,
            tr("오디오 장치"),
            new AudioDevicePage(theme, *context.settings, *context.audio, this));
    addPage(PreferencesPage::Appearance, tr("외관"), new AppearancePage(theme, *context.settings, this));
    addPage(PreferencesPage::Practice, tr("연습"), new PracticePage(theme, *context.settings, *context.audio, this));
    addPage(PreferencesPage::ScoreTab, tr("악보·탭"), new PlaceholderPage(tr("악보·탭"), theme, this));
    addPage(PreferencesPage::StemSeparation, tr("스템 분리"), new PlaceholderPage(tr("스템 분리"), theme, this));
    addPage(PreferencesPage::Shortcuts,
            tr("단축키"),
            new ShortcutsPage(theme, *context.actions, *context.keymap, this));
    addPage(PreferencesPage::Files, tr("저장·파일"), new PlaceholderPage(tr("저장·파일"), theme, this));
    addPage(PreferencesPage::Stats, tr("기록"), new PlaceholderPage(tr("기록"), theme, this));
    addPage(PreferencesPage::Video, tr("영상"), new PlaceholderPage(tr("영상"), theme, this));
    m_list->setCurrentRow(0);
}

void PreferencesDialog::buildLayout()
{
    auto *layout = new QVBoxLayout(this);
    auto *body = new QHBoxLayout();
    m_list->setFixedWidth(listWidth);
    m_list->setAccessibleName(tr("환경설정 페이지"));
    body->addWidget(m_list);
    body->addWidget(m_stack, 1);
    layout->addLayout(body, 1);
    auto *footer = new QHBoxLayout();
    footer->addWidget(m_restore);
    footer->addStretch();
    footer->addWidget(m_cancel);
    footer->addWidget(m_apply);
    layout->addLayout(footer);
    const auto current = [this] -> PreferencesPageBase * {
        return m_pages[static_cast<std::size_t>(m_stack->currentIndex())];
    };
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        m_stack->setCurrentIndex(row);
        syncButtons();
    });
    connect(m_restore, &QPushButton::clicked, this, [current] {
        current()->restoreDefaults();
    });
    connect(m_apply, &QPushButton::clicked, this, [current] {
        current()->apply();
    });
    connect(m_cancel, &QPushButton::clicked, this, [current] {
        current()->discard();
    });
}

void PreferencesDialog::addPage(PreferencesPage id, const QString &title, PreferencesPageBase *widget)
{
    Q_ASSERT(static_cast<std::size_t>(id) == m_pages.size());
    m_list->addItem(title);
    m_stack->addWidget(widget);
    m_pages.push_back(widget);
}

void PreferencesDialog::showPage(PreferencesPage page)
{
    m_list->setCurrentRow(static_cast<int>(page));
}

PreferencesPage PreferencesDialog::currentPage() const
{
    return static_cast<PreferencesPage>(m_stack->currentIndex());
}

PreferencesPageBase *PreferencesDialog::page(PreferencesPage page) const
{
    return m_pages[static_cast<std::size_t>(page)];
}

void PreferencesDialog::syncButtons()
{
    const bool apply = m_pages[static_cast<std::size_t>(m_stack->currentIndex())]->needsApply();
    m_apply->setVisible(apply);
    m_cancel->setVisible(apply);
}

} // namespace cc::ui
