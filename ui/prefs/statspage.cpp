// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/prefs/statspage.h"

#include "generated/settingkeys.h"
#include "ui/shell/settings.h"
#include "ui/theme/theme.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

#include <utility>

namespace cc::ui
{

namespace
{

constexpr int maxMinDayMinutes = 120;
constexpr int maxRetentionDays = 3650;

} // namespace

StatsPage::StatsPage(const Theme &theme, Settings &settings, std::filesystem::path userDir, QWidget *parent)
    : PreferencesPageBase(parent)
    , m_settings(&settings)
    , m_userDir(std::move(userDir))
    , m_enabled(new QCheckBox(tr("연습 기록 수집"), this))
    , m_minDayMinutes(new QSpinBox(this))
    , m_retentionDays(new QSpinBox(this))
    , m_dbPath(new QLabel(this))
{
    auto *layout = new QVBoxLayout(this);
    auto *heading = new QLabel(tr("기록"), this);
    heading->setFont(theme.font(theme::TypeStyle::Subtitle));
    layout->addWidget(heading);
    auto *form = new QFormLayout();
    m_enabled->setAccessibleName(tr("기록 수집"));
    form->addRow(QString(), m_enabled);
    form->addRow(QString(), new QLabel(tr("수집 켬/끔과 DB 위치는 다음 실행부터 적용됩니다."), this));
    m_minDayMinutes->setRange(1, maxMinDayMinutes);
    m_minDayMinutes->setSuffix(tr(" 분"));
    m_minDayMinutes->setAccessibleName(tr("하루 인정 최소 분"));
    form->addRow(tr("하루 인정 최소"), m_minDayMinutes);
    m_retentionDays->setRange(0, maxRetentionDays);
    m_retentionDays->setSuffix(tr(" 일"));
    m_retentionDays->setSpecialValueText(tr("무제한"));
    m_retentionDays->setAccessibleName(tr("보존 기간 일"));
    form->addRow(tr("보존 기간"), m_retentionDays);
    m_dbPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_dbPath->setWordWrap(true);
    m_dbPath->setAccessibleName(tr("기록 DB 위치"));
    form->addRow(tr("DB 위치"), m_dbPath);
    layout->addLayout(form);
    layout->addStretch();
    bindControls();
    syncFromSettings();
    connect(m_settings, &Settings::valueChanged, this, [this](setting::Key) {
        syncFromSettings();
    });
}

QString StatsPage::effectiveDbPath() const
{
    QString custom = m_settings->stringValue(setting::Key::StatsDbPath);
    if (!custom.isEmpty()) {
        return custom;
    }
    return QString::fromStdWString((m_userDir / "stats.db").wstring());
}

void StatsPage::bindControls()
{
    connect(m_enabled, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_syncing) {
            m_settings->setValue(setting::Key::StatsEnabled, nlohmann::json(on));
        }
    });
    connect(m_minDayMinutes, &QSpinBox::valueChanged, this, [this](int minutes) {
        if (!m_syncing) {
            m_settings->setValue(setting::Key::StatsMinDayMinutes, nlohmann::json(minutes));
        }
    });
    connect(m_retentionDays, &QSpinBox::valueChanged, this, [this](int days) {
        if (!m_syncing) {
            m_settings->setValue(setting::Key::StatsRetentionDays, nlohmann::json(days));
        }
    });
}

void StatsPage::syncFromSettings()
{
    m_syncing = true;
    m_enabled->setChecked(m_settings->boolValue(setting::Key::StatsEnabled));
    m_minDayMinutes->setValue(m_settings->intValue(setting::Key::StatsMinDayMinutes));
    m_retentionDays->setValue(m_settings->intValue(setting::Key::StatsRetentionDays));
    m_dbPath->setText(effectiveDbPath());
    m_syncing = false;
}

void StatsPage::restoreDefaults()
{
    for (const setting::Key key : {setting::Key::StatsEnabled,
                                   setting::Key::StatsMinDayMinutes,
                                   setting::Key::StatsRetentionDays,
                                   setting::Key::StatsDbPath}) {
        m_settings->setValue(key, Settings::defaultValue(key));
    }
}

} // namespace cc::ui
