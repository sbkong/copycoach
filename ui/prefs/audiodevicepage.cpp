// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/prefs/audiodevicepage.h"

#include "ui/audio/audioservice.h"
#include "ui/prefs/inlinemessage.h"
#include "ui/shell/errortext.h"
#include "ui/shell/settings.h"
#include "ui/theme/theme.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <array>
#include <cstdint>

namespace cc::ui
{

namespace
{

constexpr std::array<std::pair<const char *, const char *>, 3> hostApis{{
    {"asio", "ASIO"},
    {"wasapi-exclusive", "WASAPI 독점"},
    {"wasapi-shared", "WASAPI 공유"},
}};
constexpr std::array<int, 4> sampleRates{44100, 48000, 88200, 96000};
constexpr std::array<int, 5> bufferSizes{64, 128, 256, 512, 1024};

QWidget *rowWidget(QWidget *parent, std::initializer_list<QWidget *> items)
{
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    for (QWidget *item : items) {
        layout->addWidget(item);
    }
    layout->addStretch();
    return row;
}

bool isAsio(const QString &hostApi)
{
    return hostApi == QLatin1String(hostApis[0].first);
}

/// PortAudio reports WASAPI devices under "wasapi" whatever the mode; the setting keeps the mode.
bool deviceMatchesHost(const audio::DeviceInfo &device, const QString &hostApi)
{
    return QString::fromStdString(device.hostApi)
        == (isAsio(hostApi) ? QStringLiteral("asio") : QStringLiteral("wasapi"));
}

} // namespace

AudioDevicePage::AudioDevicePage(const Theme &theme, Settings &settings, AudioService &audio, QWidget *parent)
    : PreferencesPageBase(parent)
    , m_theme(&theme)
    , m_settings(&settings)
    , m_audio(&audio)
    , m_driver(new QComboBox(this))
    , m_controlPanel(new QPushButton(tr("제어판…"), this))
    , m_input(new QComboBox(this))
    , m_output(new QComboBox(this))
    , m_sampleRate(new QComboBox(this))
    , m_buffer(new QComboBox(this))
    , m_bufferNote(new QLabel(this))
    , m_latency(new QLabel(this))
    , m_testTone(new QPushButton(tr("테스트 톤"), this))
    , m_refresh(new QPushButton(tr("장치 새로고침"), this))
    , m_message(new InlineMessage(theme, this))
{
    auto *layout = new QVBoxLayout(this);
    auto *heading = new QLabel(tr("오디오 장치"), this);
    heading->setFont(theme.font(theme::TypeStyle::Subtitle));
    layout->addWidget(heading);
    auto *form = new QFormLayout();
    buildSystemRow(form);
    buildDeviceRows(form);
    buildFormatRow(form);
    buildLatencyRow(form);
    layout->addLayout(form);
    layout->addWidget(m_message);
    layout->addWidget(new QLabel(
        tr("이 페이지만 [적용]으로 반영됩니다 (스트림 재시작). 채널 용도 표와 루프백 측정, 프로필은 다음 조각."),
        this));
    layout->addStretch();
    refreshDevices();
    loadFromSettings();
    showDeviceState();
}

void AudioDevicePage::buildSystemRow(QFormLayout *form)
{
    auto *row = new QWidget(this);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    for (const auto &[value, label] : hostApis) {
        auto *button = new QToolButton(row);
        button->setText(tr(label));
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setAccessibleName(tr("오디오 시스템 %1").arg(tr(label)));
        connect(button, &QToolButton::clicked, this, &AudioDevicePage::syncRows);
        layout->addWidget(button);
        m_systemButtons.push_back(button);
    }
    layout->addStretch();
    form->addRow(tr("오디오 시스템"), row);
}

void AudioDevicePage::buildDeviceRows(QFormLayout *form)
{
    m_driver->setAccessibleName(tr("ASIO 드라이버"));
    m_driverRow = rowWidget(this, {m_driver, m_controlPanel});
    form->addRow(tr("ASIO 드라이버"), m_driverRow);
    m_input->setAccessibleName(tr("입력 장치"));
    m_inputRow = rowWidget(this, {m_input});
    form->addRow(tr("입력 장치"), m_inputRow);
    m_output->setAccessibleName(tr("출력 장치"));
    m_outputRow = rowWidget(this, {m_output, m_refresh});
    form->addRow(tr("출력 장치"), m_outputRow);
    connect(m_controlPanel, &QPushButton::clicked, this, &AudioDevicePage::openControlPanel);
    connect(m_refresh, &QPushButton::clicked, this, [this] {
        refreshDevices();
        loadFromSettings();
    });
}

void AudioDevicePage::buildFormatRow(QFormLayout *form)
{
    for (const int rate : sampleRates) {
        m_sampleRate->addItem(QString::number(rate), rate);
    }
    m_sampleRate->setAccessibleName(tr("샘플레이트"));
    for (const int frames : bufferSizes) {
        m_buffer->addItem(QString::number(frames), frames);
    }
    m_buffer->setAccessibleName(tr("버퍼 크기"));
    form->addRow(tr("샘플레이트 · 버퍼"), rowWidget(this, {m_sampleRate, m_buffer, m_bufferNote}));
}

void AudioDevicePage::buildLatencyRow(QFormLayout *form)
{
    m_latency->setFont(m_theme->font(theme::TypeStyle::Body, true));
    connect(m_testTone, &QPushButton::clicked, m_audio, &AudioService::testTone);
    form->addRow(tr("지연"), rowWidget(this, {m_latency, m_testTone}));
}

QString AudioDevicePage::hostApi() const
{
    for (std::size_t i = 0; i < m_systemButtons.size(); ++i) {
        if (m_systemButtons[i]->isChecked()) {
            return QLatin1String(hostApis[i].first);
        }
    }
    return QLatin1String(hostApis.back().first);
}

void AudioDevicePage::refreshDevices()
{
    m_devices = m_audio->devices();
}

void AudioDevicePage::fillDeviceCombo(QComboBox *combo, bool input, const QString &current)
{
    combo->clear();
    combo->addItem(tr("기본 장치"), QString());
    for (const audio::DeviceInfo &device : m_devices) {
        const bool usable = input ? device.maxInputChannels > 0 : device.maxOutputChannels > 0;
        if (usable && deviceMatchesHost(device, hostApi())) {
            combo->addItem(QString::fromStdString(device.name), QString::fromStdString(device.name));
        }
    }
    const int index = combo->findData(current);
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

void AudioDevicePage::loadFromSettings()
{
    const QString api = m_settings->stringValue(setting::Key::AudioBackend);
    for (std::size_t i = 0; i < m_systemButtons.size(); ++i) {
        m_systemButtons[i]->setChecked(api == QLatin1String(hostApis[i].first));
    }
    m_sampleRate->setCurrentIndex(
        std::max(0, m_sampleRate->findData(m_settings->intValue(setting::Key::AudioSampleRate))));
    m_buffer->setCurrentIndex(std::max(0, m_buffer->findData(m_settings->intValue(setting::Key::AudioBufferFrames))));
    syncRows();
}

/// ASIO shows one driver row; WASAPI shows input / output rows; the buffer is the app's only under WASAPI.
void AudioDevicePage::syncRows()
{
    const bool asio = isAsio(hostApi());
    m_driverRow->setVisible(asio);
    m_inputRow->setVisible(!asio);
    m_outputRow->setVisible(!asio);
    m_buffer->setEnabled(!asio);
    m_bufferNote->setText(asio ? tr("버퍼는 드라이버 제어판에서") : tr("프레임"));
    const QString output = m_settings->stringValue(setting::Key::AudioOutputDevice);
    fillDeviceCombo(asio ? m_driver : m_output, false, output);
    if (!asio) {
        fillDeviceCombo(m_input, true, m_settings->stringValue(setting::Key::AudioInputDevice));
    }
}

void AudioDevicePage::showDeviceState()
{
    if (const auto info = m_audio->streamInfo()) {
        m_latency->setText(tr("입력 %1 ms · 출력 %2 ms · 왕복 %3 ms (보고값)")
                               .arg(info->inputLatencySeconds * 1000.0, 0, 'f', 1)
                               .arg(info->outputLatencySeconds * 1000.0, 0, 'f', 1)
                               .arg((info->inputLatencySeconds + info->outputLatencySeconds) * 1000.0, 0, 'f', 1));
        m_message->clear();
        return;
    }
    m_latency->setText(tr("장치 없음"));
    if (const auto error = m_audio->deviceError()) {
        showError(*error);
    }
}

void AudioDevicePage::showError(const Error &error)
{
    const QString text =
        errorText(error) + QStringLiteral(" ") + tr("장치를 다시 연결하거나 WASAPI 공유로 전환하세요.");
    m_message->show(MessageKind::Negative,
                    text,
                    {MessageAction{tr("WASAPI 공유로 전환"),
                                   [this] {
                                       m_systemButtons.back()->setChecked(true);
                                       syncRows();
                                       apply();
                                   }},
                     MessageAction{tr("다시 시도"), [this] {
                                       apply();
                                   }}});
}

void AudioDevicePage::openControlPanel()
{
    const QString driver = m_driver->currentData().toString();
    if (const auto r = m_audio->showAsioControlPanel(driver, static_cast<std::uintptr_t>(window()->winId())); !r) {
        m_message->show(MessageKind::Notice, errorText(r.error()), {});
    }
}

bool AudioDevicePage::hasMessage() const
{
    return m_message->isVisible();
}

bool AudioDevicePage::needsApply() const
{
    return true;
}

void AudioDevicePage::apply()
{
    m_settings->setValue(setting::Key::AudioBackend, hostApi());
    const bool asio = isAsio(hostApi());
    m_settings->setValue(setting::Key::AudioOutputDevice, (asio ? m_driver : m_output)->currentData().toString());
    m_settings->setValue(setting::Key::AudioInputDevice, asio ? QString() : m_input->currentData().toString());
    m_settings->setValue(setting::Key::AudioSampleRate, nlohmann::json(m_sampleRate->currentData().toInt()));
    m_settings->setValue(setting::Key::AudioBufferFrames, nlohmann::json(m_buffer->currentData().toInt()));
    if (const auto r = m_audio->reopenDevice(); !r) {
        showError(r.error());
        return;
    }
    showDeviceState();
}

void AudioDevicePage::discard()
{
    loadFromSettings();
    showDeviceState();
}

void AudioDevicePage::restoreDefaults()
{
    for (const setting::Key key : {setting::Key::AudioBackend,
                                   setting::Key::AudioInputDevice,
                                   setting::Key::AudioOutputDevice,
                                   setting::Key::AudioSampleRate,
                                   setting::Key::AudioBufferFrames}) {
        m_settings->setValue(key, Settings::defaultValue(key));
    }
    loadFromSettings();
}

} // namespace cc::ui
