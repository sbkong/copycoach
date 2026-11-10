// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/copy/stemmixer.h"

#include "ui/theme/theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <array>

namespace cc::ui
{

namespace
{

constexpr int gainSteps = 100;
constexpr int chipSizePx = 10;
constexpr std::array<theme::Token, 4> stemColors{theme::Token::StemVocalsColor,
                                                 theme::Token::StemDrumsColor,
                                                 theme::Token::StemBassColor,
                                                 theme::Token::StemOtherColor};

} // namespace

StemMixerPanel::StemMixerPanel(const Theme &theme, QWidget *parent)
    : QWidget(parent)
    , m_theme(&theme)
    , m_empty(new QLabel(tr("스템이 아직 없습니다. 분리하면 보컬·드럼·베이스·나머지를 따로 켜고 끌 수 있습니다"), this))
    , m_separate(new QPushButton(tr("스템 분리…"), this))
    , m_rowsLayout(new QVBoxLayout())
{
    setAccessibleName(tr("스템 믹서"));
    auto *layout = new QVBoxLayout(this);
    m_empty->setWordWrap(true);
    m_separate->setAccessibleName(tr("스템 분리"));
    layout->addWidget(m_empty);
    layout->addWidget(m_separate, 0, Qt::AlignLeft);
    layout->addLayout(m_rowsLayout);
    layout->addStretch();
    connect(m_separate, &QPushButton::clicked, this, &StemMixerPanel::separateRequested);
}

StemMixerPanel::Row StemMixerPanel::makeRow(std::size_t index, const QString &name)
{
    Row row;
    row.widget = new QWidget(this);
    auto *layout = new QHBoxLayout(row.widget);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *chip = new QFrame(row.widget);
    chip->setFixedSize(chipSizePx, chipSizePx);
    chip->setAutoFillBackground(true);
    QPalette pal = chip->palette();
    pal.setColor(QPalette::Window, m_theme->color(stemColors[index % stemColors.size()]));
    chip->setPalette(pal);
    auto *label = new QLabel(name, row.widget);
    row.mute = new QToolButton(row.widget);
    row.mute->setText(tr("M"));
    row.mute->setCheckable(true);
    row.mute->setAccessibleName(tr("%1 뮤트").arg(name));
    row.solo = new QToolButton(row.widget);
    row.solo->setText(tr("S"));
    row.solo->setCheckable(true);
    row.solo->setAccessibleName(tr("%1 솔로").arg(name));
    row.gain = new QSlider(Qt::Horizontal, row.widget);
    row.gain->setRange(0, gainSteps);
    row.gain->setValue(gainSteps);
    row.gain->setAccessibleName(tr("%1 볼륨").arg(name));
    layout->addWidget(chip);
    layout->addWidget(label, 1);
    layout->addWidget(row.mute);
    layout->addWidget(row.solo);
    layout->addWidget(row.gain, 2);
    connect(row.mute, &QToolButton::toggled, this, [this, index](bool on) {
        emit muteChanged(index, on);
    });
    connect(row.solo, &QToolButton::toggled, this, [this, index](bool on) {
        emit soloChanged(index, on);
    });
    connect(row.gain, &QSlider::valueChanged, this, [this, index](int value) {
        emit gainChanged(index, static_cast<float>(value) / gainSteps);
    });
    return row;
}

void StemMixerPanel::setStems(const QStringList &names)
{
    clear();
    for (qsizetype i = 0; i < names.size(); ++i) {
        const Row row = makeRow(static_cast<std::size_t>(i), names[i]);
        m_rowsLayout->addWidget(row.widget);
        m_rows.push_back(row);
    }
    const bool any = !m_rows.empty();
    m_empty->setVisible(!any);
    m_separate->setVisible(!any);
}

void StemMixerPanel::clear()
{
    for (const Row &row : m_rows) {
        row.widget->deleteLater();
    }
    m_rows.clear();
    m_empty->setVisible(true);
    m_separate->setVisible(true);
}

bool StemMixerPanel::hasStems() const
{
    return !m_rows.empty();
}

std::size_t StemMixerPanel::stemCount() const
{
    return m_rows.size();
}

void StemMixerPanel::setBusy(bool busy)
{
    m_separate->setEnabled(!busy);
}

void StemMixerPanel::setMuted(std::size_t stem, bool muted)
{
    if (stem < m_rows.size()) {
        m_rows[stem].mute->setChecked(muted);
    }
}

void StemMixerPanel::setSolo(std::size_t stem, bool solo)
{
    if (stem < m_rows.size()) {
        m_rows[stem].solo->setChecked(solo);
    }
}

void StemMixerPanel::toggleMute(std::size_t stem)
{
    setMuted(stem, !isMuted(stem));
}

void StemMixerPanel::toggleSolo(std::size_t stem)
{
    setSolo(stem, !isSolo(stem));
}

bool StemMixerPanel::isMuted(std::size_t stem) const
{
    return stem < m_rows.size() && m_rows[stem].mute->isChecked();
}

bool StemMixerPanel::isSolo(std::size_t stem) const
{
    return stem < m_rows.size() && m_rows[stem].solo->isChecked();
}

QAbstractButton *StemMixerPanel::separateButton() const
{
    return m_separate;
}

} // namespace cc::ui
