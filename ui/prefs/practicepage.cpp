// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/prefs/practicepage.h"

#include "core/theory/constants.h"
#include "ui/audio/audioservice.h"
#include "ui/shell/settings.h"
#include "ui/theme/theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace cc::ui
{

namespace
{

constexpr int maxCountInBars = 4;
constexpr int maxTimerMinutes = 120;
constexpr std::array<const char *, 5> timeSignatures{"4/4", "3/4", "6/8", "2/4", "5/4"};
constexpr std::array<const char *, 4> timerLabels{"01 스케일", "02 쉘+드랍2", "03 트라이어드", "04 라인"};

} // namespace

PracticePage::PracticePage(const Theme &theme, Settings &settings, AudioService &audio, QWidget *parent)
    : PreferencesPageBase(parent)
    , m_theme(&theme)
    , m_settings(&settings)
    , m_audio(&audio)
    , m_countIn(new QSpinBox(this))
    , m_metronome(new QComboBox(this))
    , m_guitar(new QComboBox(this))
    , m_piano(new QComboBox(this))
    , m_tempo(new QSpinBox(this))
    , m_timeSig(new QComboBox(this))
    , m_autoAssign(new QCheckBox(tr("요일 루틴 자동 배정"), this))
    , m_autosave(new QCheckBox(tr("연습 세션 자동 저장"), this))
{
    auto *layout = new QVBoxLayout(this);
    auto *heading = new QLabel(tr("연습"), this);
    heading->setFont(theme.font(theme::TypeStyle::Subtitle));
    layout->addWidget(heading);
    auto *form = new QFormLayout();
    m_countIn->setRange(0, maxCountInBars);
    m_countIn->setSuffix(tr(" 마디"));
    m_countIn->setAccessibleName(tr("카운트인 마디 수"));
    form->addRow(tr("카운트인"), m_countIn);
    buildSoundRows(form);
    buildTimerRow(form);
    m_tempo->setRange(static_cast<int>(theory::tempoMinBpm), static_cast<int>(theory::tempoMaxBpm));
    m_tempo->setSuffix(tr(" BPM"));
    m_tempo->setAccessibleName(tr("기본 템포"));
    form->addRow(tr("기본 템포"), m_tempo);
    for (const char *sig : timeSignatures) {
        m_timeSig->addItem(QString::fromLatin1(sig), QString::fromLatin1(sig));
    }
    m_timeSig->setAccessibleName(tr("기본 박자표"));
    form->addRow(tr("기본 박자표"), m_timeSig);
    form->addRow(QString(), m_autoAssign);
    form->addRow(QString(), m_autosave);
    layout->addLayout(form);
    layout->addStretch();
    bindSimpleControls();
    syncFromSettings();
    connect(m_settings, &Settings::valueChanged, this, [this](setting::Key) {
        syncFromSettings();
    });
}

void PracticePage::fillSoundCombo(QComboBox *combo, int kind)
{
    const audio::SoundCatalog *catalog = m_audio->catalog();
    if (catalog == nullptr) {
        return;
    }
    for (const audio::SoundEntry &entry : catalog->entries) {
        if (static_cast<int>(entry.kind) == kind) {
            combo->addItem(QString::fromStdString(entry.label), QString::fromStdString(entry.id));
        }
    }
}

void PracticePage::buildSoundRows(QFormLayout *form)
{
    fillSoundCombo(m_metronome, static_cast<int>(audio::SoundKind::Metronome));
    fillSoundCombo(m_guitar, static_cast<int>(audio::SoundKind::Guitar));
    fillSoundCombo(m_piano, static_cast<int>(audio::SoundKind::Piano));
    m_metronome->setAccessibleName(tr("메트로놈 소리"));
    m_guitar->setAccessibleName(tr("기타 음색"));
    m_piano->setAccessibleName(tr("피아노 음색"));
    form->addRow(tr("메트로놈 소리"), m_metronome);
    form->addRow(tr("기타 음색"), m_guitar);
    form->addRow(tr("피아노 음색"), m_piano);
    bindSoundCombo(m_metronome, setting::Key::PracticeMetronomeSound);
    bindSoundCombo(m_guitar, setting::Key::PracticeGuitarSound);
    bindSoundCombo(m_piano, setting::Key::PracticePianoSound);
}

void PracticePage::bindSoundCombo(QComboBox *combo, setting::Key key)
{
    connect(combo, &QComboBox::currentIndexChanged, this, [this, combo, key](int index) {
        if (!m_syncing) {
            m_settings->setValue(key, combo->itemData(index).toString());
            m_audio->applySoundSettings();
        }
    });
}

void PracticePage::buildTimerRow(QFormLayout *form)
{
    auto *row = new QWidget(this);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    for (std::size_t i = 0; i < m_timer.size(); ++i) {
        m_timer[i] = new QSpinBox(row);
        m_timer[i]->setRange(1, maxTimerMinutes);
        m_timer[i]->setSuffix(tr(" 분"));
        m_timer[i]->setAccessibleName(tr("타이머 %1").arg(tr(timerLabels[i])));
        m_timer[i]->setToolTip(tr(timerLabels[i]));
        layout->addWidget(m_timer[i]);
        connect(m_timer[i], &QSpinBox::valueChanged, this, [this](int) {
            saveTimerMinutes();
        });
    }
    layout->addStretch();
    form->addRow(tr("타이머 기본 (분)"), row);
}

void PracticePage::saveTimerMinutes()
{
    if (m_syncing) {
        return;
    }
    nlohmann::json minutes = nlohmann::json::array();
    for (const QSpinBox *spin : m_timer) {
        minutes.push_back(spin->value());
    }
    m_settings->setValue(setting::Key::PracticeTimerMinutes, minutes);
}

void PracticePage::bindSimpleControls()
{
    connect(m_countIn, &QSpinBox::valueChanged, this, [this](int bars) {
        if (!m_syncing) {
            m_settings->setValue(setting::Key::PracticeCountInBars, nlohmann::json(bars));
        }
    });
    connect(m_tempo, &QSpinBox::valueChanged, this, [this](int bpm) {
        if (!m_syncing) {
            m_settings->setValue(setting::Key::PracticeTempoDefault, nlohmann::json(bpm));
        }
    });
    connect(m_timeSig, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!m_syncing) {
            m_settings->setValue(setting::Key::PracticeTimeSignatureDefault, m_timeSig->itemData(index).toString());
        }
    });
    connect(m_autoAssign, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_syncing) {
            m_settings->setValue(setting::Key::PracticeAutoAssignWeekday, nlohmann::json(on));
        }
    });
    connect(m_autosave, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_syncing) {
            m_settings->setValue(setting::Key::PracticeSessionAutosave, nlohmann::json(on));
        }
    });
}

void PracticePage::syncFromSettings()
{
    m_syncing = true;
    m_countIn->setValue(m_settings->intValue(setting::Key::PracticeCountInBars));
    m_metronome->setCurrentIndex(
        std::max(0, m_metronome->findData(m_settings->stringValue(setting::Key::PracticeMetronomeSound))));
    m_guitar->setCurrentIndex(
        std::max(0, m_guitar->findData(m_settings->stringValue(setting::Key::PracticeGuitarSound))));
    m_piano->setCurrentIndex(std::max(0, m_piano->findData(m_settings->stringValue(setting::Key::PracticePianoSound))));
    const nlohmann::json minutes = m_settings->value(setting::Key::PracticeTimerMinutes);
    for (std::size_t i = 0; i < m_timer.size(); ++i) {
        m_timer[i]->setValue(minutes.is_array() && minutes.size() > i ? minutes[i].get<int>() : 10);
    }
    m_tempo->setValue(m_settings->intValue(setting::Key::PracticeTempoDefault));
    m_timeSig->setCurrentIndex(
        std::max(0, m_timeSig->findData(m_settings->stringValue(setting::Key::PracticeTimeSignatureDefault))));
    m_autoAssign->setChecked(m_settings->boolValue(setting::Key::PracticeAutoAssignWeekday));
    m_autosave->setChecked(m_settings->boolValue(setting::Key::PracticeSessionAutosave));
    m_syncing = false;
}

void PracticePage::restoreDefaults()
{
    for (const setting::Key key : {setting::Key::PracticeCountInBars,
                                   setting::Key::PracticeMetronomeSound,
                                   setting::Key::PracticeGuitarSound,
                                   setting::Key::PracticePianoSound,
                                   setting::Key::PracticeTimerMinutes,
                                   setting::Key::PracticeTempoDefault,
                                   setting::Key::PracticeTimeSignatureDefault,
                                   setting::Key::PracticeAutoAssignWeekday,
                                   setting::Key::PracticeSessionAutosave}) {
        m_settings->setValue(key, Settings::defaultValue(key));
    }
    m_audio->applySoundSettings();
}

} // namespace cc::ui
