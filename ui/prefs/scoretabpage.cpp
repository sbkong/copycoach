// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/prefs/scoretabpage.h"

#include "ui/shell/settings.h"
#include "ui/theme/theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace cc::ui
{

namespace
{

constexpr int maxCapoFret = 12;

void selectData(QComboBox *combo, const QString &data)
{
    const int index = combo->findData(data);
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

} // namespace

ScoreTabPage::ScoreTabPage(const Theme &theme, Settings &settings, QWidget *parent)
    : PreferencesPageBase(parent)
    , m_settings(&settings)
    , m_tuning(new QComboBox(this))
    , m_capo(new QSpinBox(this))
    , m_notation(new QComboBox(this))
    , m_instrumentMode(new QComboBox(this))
    , m_followCursor(new QCheckBox(tr("재생 중 커서 뒤따름"), this))
    , m_fingeringPreset(new QComboBox(this))
{
    auto *layout = new QVBoxLayout(this);
    auto *heading = new QLabel(tr("악보·탭"), this);
    heading->setFont(theme.font(theme::TypeStyle::Subtitle));
    layout->addWidget(heading);
    auto *form = new QFormLayout();
    buildForm(form);
    layout->addLayout(form);
    layout->addStretch();

    bindEnumCombo(m_tuning, setting::Key::ScoreDefaultTuning);
    bindEnumCombo(m_notation, setting::Key::ScoreNotation);
    bindEnumCombo(m_instrumentMode, setting::Key::ScoreInstrumentMode);
    bindEnumCombo(m_fingeringPreset, setting::Key::ScoreFingeringPreset);
    connect(m_capo, &QSpinBox::valueChanged, this, [this](int fret) {
        if (!m_syncing) {
            m_settings->setValue(setting::Key::ScoreDefaultCapo, nlohmann::json(fret));
        }
    });
    connect(m_followCursor, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_syncing) {
            m_settings->setValue(setting::Key::ScoreFollowCursor, nlohmann::json(on));
        }
    });
    syncFromSettings();
    connect(m_settings, &Settings::valueChanged, this, [this](setting::Key) {
        syncFromSettings();
    });
}

void ScoreTabPage::buildForm(QFormLayout *form)
{
    m_tuning->addItem(tr("표준 6현 (E A D G B E)"),
                      QStringLiteral("standard-6")); // the only tuning the model ships yet
    m_tuning->setAccessibleName(tr("기본 튜닝"));
    form->addRow(tr("기본 튜닝"), m_tuning);
    m_capo->setRange(0, maxCapoFret);
    m_capo->setSuffix(tr(" 프렛"));
    m_capo->setSpecialValueText(tr("없음"));
    m_capo->setAccessibleName(tr("기본 카포"));
    form->addRow(tr("기본 카포"), m_capo);
    m_notation->addItem(tr("오선 + 탭"), QStringLiteral("staff-tab"));
    m_notation->addItem(tr("탭만"), QStringLiteral("tab"));
    m_notation->setAccessibleName(tr("표기 기본"));
    form->addRow(tr("표기"), m_notation);
    m_instrumentMode->addItem(tr("연주"), QStringLiteral("play"));
    m_instrumentMode->addItem(tr("입력"), QStringLiteral("input"));
    m_instrumentMode->setAccessibleName(tr("인스트루먼트 뷰 기본 모드"));
    form->addRow(tr("인스트루먼트 뷰"), m_instrumentMode);
    m_followCursor->setAccessibleName(tr("커서 뒤따름"));
    form->addRow(QString(), m_followCursor);
    m_fingeringPreset->addItem(tr("블럭"), QStringLiteral("block"));
    m_fingeringPreset->addItem(tr("라인"), QStringLiteral("line"));
    m_fingeringPreset->setAccessibleName(tr("운지 프리셋"));
    form->addRow(tr("운지 프리셋"), m_fingeringPreset);
    form->addRow(QString(), new QLabel(tr("곡별 값은 편집기 인스펙터의 트랙 항목에서 바꿉니다."), this));
}

void ScoreTabPage::bindEnumCombo(QComboBox *combo, setting::Key key)
{
    connect(combo, &QComboBox::currentIndexChanged, this, [this, combo, key](int index) {
        if (!m_syncing && index >= 0) {
            m_settings->setValue(key, combo->itemData(index).toString());
        }
    });
}

void ScoreTabPage::syncFromSettings()
{
    m_syncing = true;
    selectData(m_tuning, m_settings->stringValue(setting::Key::ScoreDefaultTuning));
    m_capo->setValue(m_settings->intValue(setting::Key::ScoreDefaultCapo));
    selectData(m_notation, m_settings->stringValue(setting::Key::ScoreNotation));
    selectData(m_instrumentMode, m_settings->stringValue(setting::Key::ScoreInstrumentMode));
    m_followCursor->setChecked(m_settings->boolValue(setting::Key::ScoreFollowCursor));
    selectData(m_fingeringPreset, m_settings->stringValue(setting::Key::ScoreFingeringPreset));
    m_syncing = false;
}

void ScoreTabPage::restoreDefaults()
{
    for (const setting::Key key : {setting::Key::ScoreDefaultTuning,
                                   setting::Key::ScoreDefaultCapo,
                                   setting::Key::ScoreNotation,
                                   setting::Key::ScoreInstrumentMode,
                                   setting::Key::ScoreFollowCursor,
                                   setting::Key::ScoreFingeringPreset}) {
        m_settings->setValue(key, Settings::defaultValue(key));
    }
}

} // namespace cc::ui
