// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/prefs/videopage.h"

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

constexpr int minFrameCacheMb = 64;
constexpr int maxFrameCacheMb = 8192;
constexpr int frameCacheStepMb = 64;

} // namespace

VideoPage::VideoPage(const Theme &theme, Settings &settings, QWidget *parent)
    : PreferencesPageBase(parent)
    , m_settings(&settings)
    , m_hwDecode(new QCheckBox(tr("하드웨어 디코딩"), this))
    , m_frameCache(new QSpinBox(this))
    , m_audioCacheDir(new PathRow(settings, setting::Key::VideoAudioCacheDir, tr("프로젝트의 cache/ 폴더"), this))
{
    auto *layout = new QVBoxLayout(this);
    auto *heading = new QLabel(tr("영상"), this);
    heading->setFont(theme.font(theme::TypeStyle::Subtitle));
    layout->addWidget(heading);
    auto *form = new QFormLayout();
    m_hwDecode->setAccessibleName(tr("하드웨어 디코딩"));
    form->addRow(QString(), m_hwDecode);
    form->addRow(
        QString(),
        new QLabel(tr("지금 버전은 CPU로만 디코딩합니다. 하드웨어 디코딩과 프레임 캐시는 다음 조각에서 읽습니다."),
                   this));
    m_frameCache->setRange(minFrameCacheMb, maxFrameCacheMb);
    m_frameCache->setSingleStep(frameCacheStepMb);
    m_frameCache->setSuffix(tr(" MB"));
    m_frameCache->setAccessibleName(tr("프레임 캐시 크기"));
    form->addRow(tr("프레임 캐시"), m_frameCache);
    m_audioCacheDir->setAccessibleName(tr("추출 오디오 캐시 위치"));
    form->addRow(tr("추출 오디오 캐시"), m_audioCacheDir);
    layout->addLayout(form);
    layout->addStretch();
    bindControls();
    syncFromSettings();
    connect(m_settings, &Settings::valueChanged, this, [this](setting::Key) {
        syncFromSettings();
    });
}

void VideoPage::bindControls()
{
    connect(m_hwDecode, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_syncing) {
            m_settings->setValue(setting::Key::VideoHwDecode, nlohmann::json(on));
        }
    });
    connect(m_frameCache, &QSpinBox::valueChanged, this, [this](int mb) {
        if (!m_syncing) {
            m_settings->setValue(setting::Key::VideoFrameCacheMb, nlohmann::json(mb));
        }
    });
}

void VideoPage::syncFromSettings()
{
    m_syncing = true;
    m_hwDecode->setChecked(m_settings->boolValue(setting::Key::VideoHwDecode));
    m_frameCache->setValue(m_settings->intValue(setting::Key::VideoFrameCacheMb));
    m_audioCacheDir->syncFromSettings();
    m_syncing = false;
}

void VideoPage::restoreDefaults()
{
    for (const setting::Key key :
         {setting::Key::VideoHwDecode, setting::Key::VideoFrameCacheMb, setting::Key::VideoAudioCacheDir}) {
        m_settings->setValue(key, Settings::defaultValue(key));
    }
}

} // namespace cc::ui
