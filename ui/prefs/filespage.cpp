// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/prefs/filespage.h"

#include "ui/prefs/pathrow.h"
#include "ui/shell/settings.h"
#include "ui/theme/theme.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace cc::ui
{

namespace
{

constexpr int minAutosaveS = 5;
constexpr int maxAutosaveS = 600;
constexpr int maxRecent = 30;

} // namespace

FilesPage::FilesPage(const Theme &theme, Settings &settings, QWidget *parent)
    : PreferencesPageBase(parent)
    , m_settings(&settings)
    , m_autosave(new QSpinBox(this))
    , m_projectDir(new PathRow(settings, setting::Key::FilesProjectDir, tr("문서 폴더의 CopyCoach"), this))
    , m_recentCount(new QSpinBox(this))
    , m_copyMedia(new QCheckBox(tr("불러올 때 프로젝트 폴더로 복사"), this))
{
    auto *layout = new QVBoxLayout(this);
    auto *heading = new QLabel(tr("저장·파일"), this);
    heading->setFont(theme.font(theme::TypeStyle::Subtitle));
    layout->addWidget(heading);
    auto *form = new QFormLayout();
    m_autosave->setRange(minAutosaveS, maxAutosaveS);
    m_autosave->setSuffix(tr(" 초"));
    m_autosave->setAccessibleName(tr("자동 저장 간격"));
    form->addRow(tr("자동 저장 간격"), m_autosave);
    m_projectDir->setAccessibleName(tr("프로젝트 기본 폴더"));
    form->addRow(tr("프로젝트 기본 폴더"), m_projectDir);
    m_recentCount->setRange(1, maxRecent);
    m_recentCount->setSuffix(tr(" 개"));
    m_recentCount->setAccessibleName(tr("최근 목록 길이"));
    form->addRow(tr("최근 목록"), m_recentCount);
    m_copyMedia->setAccessibleName(tr("프로젝트 폴더로 복사"));
    form->addRow(QString(), m_copyMedia);
    form->addRow(QString(),
                 new QLabel(tr("복사를 끄면 원본 경로를 그대로 참조합니다 (파일을 옮기면 끊어집니다)."), this));
    layout->addLayout(form);
    layout->addStretch();
    bindControls();
    syncFromSettings();
    connect(m_settings, &Settings::valueChanged, this, [this](setting::Key) {
        syncFromSettings();
    });
}

void FilesPage::bindControls()
{
    connect(m_autosave, &QSpinBox::valueChanged, this, [this](int seconds) {
        if (!m_syncing) {
            m_settings->setValue(setting::Key::FilesAutosaveIntervalS, nlohmann::json(seconds));
        }
    });
    connect(m_recentCount, &QSpinBox::valueChanged, this, [this](int count) {
        if (!m_syncing) {
            m_settings->setValue(setting::Key::FilesRecentCount, nlohmann::json(count));
        }
    });
    connect(m_copyMedia, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_syncing) {
            m_settings->setValue(setting::Key::FilesCopyMediaIntoProject, nlohmann::json(on));
        }
    });
}

void FilesPage::syncFromSettings()
{
    m_syncing = true;
    m_autosave->setValue(m_settings->intValue(setting::Key::FilesAutosaveIntervalS));
    m_recentCount->setValue(m_settings->intValue(setting::Key::FilesRecentCount));
    m_copyMedia->setChecked(m_settings->boolValue(setting::Key::FilesCopyMediaIntoProject));
    m_projectDir->syncFromSettings();
    m_syncing = false;
}

void FilesPage::restoreDefaults()
{
    for (const setting::Key key : {setting::Key::FilesAutosaveIntervalS,
                                   setting::Key::FilesProjectDir,
                                   setting::Key::FilesRecentCount,
                                   setting::Key::FilesCopyMediaIntoProject}) {
        m_settings->setValue(key, Settings::defaultValue(key));
    }
}

} // namespace cc::ui
