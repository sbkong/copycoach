// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/keyboard/keyboardstrip.h"

#include "ui/theme/theme.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <array>

namespace cc::ui
{

namespace
{

using theme::Token;

constexpr int whiteKeysPerOctave = 7;
constexpr std::array<int, 12> whiteIndexOfPitchClass{0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6}; // C..B -> white key
constexpr std::array<bool, 12> isBlack{false, true, false, true, false, false, true, false, true, false, true, false};
constexpr qreal blackKeyWidthRatio = 0.6;
constexpr qreal blackKeyHeightRatio = 0.62;
constexpr qreal markRadius = 7.0;

} // namespace

KeyboardStrip::KeyboardStrip(const Theme &theme, QWidget *parent)
    : QWidget(parent)
    , m_theme(&theme)
{
    setFixedHeight(stripHeight);
    setAccessibleName(tr("건반 보조 스트립"));
}

void KeyboardStrip::setMarks(std::vector<KeyMark> marks)
{
    m_marks = std::move(marks);
    if (!m_marks.empty()) {
        const auto lowest = std::ranges::min_element(m_marks, {}, &KeyMark::pitch)->pitch;
        m_firstPitch = lowest - theory::wrapPitchClass(lowest); // start of that octave
    }
    update();
}

QSize KeyboardStrip::sizeHint() const
{
    return {octaves * whiteKeysPerOctave * 24, stripHeight};
}

qreal KeyboardStrip::whiteKeyWidth() const
{
    return static_cast<qreal>(width()) / (octaves * whiteKeysPerOctave);
}

QRectF KeyboardStrip::keyRect(theory::Midi pitch) const
{
    const int offset = pitch - m_firstPitch;
    const int octave = offset / theory::semitonesPerOctave;
    const auto pc = static_cast<std::size_t>(theory::wrapPitchClass(offset));
    const qreal w = whiteKeyWidth();
    const qreal whiteX = ((octave * whiteKeysPerOctave) + whiteIndexOfPitchClass[pc]) * w;
    if (isBlack[pc]) {
        const qreal bw = w * blackKeyWidthRatio;
        return {whiteX + w - (bw / 2.0), 0.0, bw, height() * blackKeyHeightRatio};
    }
    return {whiteX, 0.0, w, static_cast<qreal>(height())};
}

void KeyboardStrip::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), m_theme->color(Token::BackgroundContentColor));
    const int count = octaves * theory::semitonesPerOctave;
    painter.setPen(m_theme->color(Token::KeyboardKeyOutlineColor));
    for (int i = 0; i < count; ++i) {
        if (!isBlack[static_cast<std::size_t>(i % theory::semitonesPerOctave)]) {
            painter.setBrush(m_theme->color(Token::KeyboardWhiteKeyColor));
            painter.drawRect(keyRect(m_firstPitch + i));
        }
    }
    for (int i = 0; i < count; ++i) {
        if (isBlack[static_cast<std::size_t>(i % theory::semitonesPerOctave)]) {
            painter.setBrush(m_theme->color(Token::KeyboardBlackKeyColor));
            painter.drawRect(keyRect(m_firstPitch + i));
        }
    }
    painter.setPen(QPen(m_theme->color(Token::BackgroundContentColor), 1.5));
    for (const KeyMark &mark : m_marks) {
        if (mark.pitch < m_firstPitch || mark.pitch >= m_firstPitch + count) {
            continue;
        }
        const QRectF key = keyRect(mark.pitch);
        const QPointF center(key.center().x(), key.bottom() - markRadius - 4.0);
        painter.setBrush(mark.fill);
        if (mark.shape == DotShape::Diamond) {
            painter.save();
            painter.translate(center);
            painter.rotate(45.0);
            painter.drawRect(QRectF(-markRadius * 0.8, -markRadius * 0.8, markRadius * 1.6, markRadius * 1.6));
            painter.restore();
        } else {
            painter.drawEllipse(center, markRadius, markRadius);
        }
    }
}

theory::Midi KeyboardStrip::pitchAt(QPointF point) const
{
    const int count = octaves * theory::semitonesPerOctave;
    for (int i = 0; i < count; ++i) { // black keys sit on top: test them first
        const theory::Midi pitch = m_firstPitch + i;
        if (isBlack[static_cast<std::size_t>(i % theory::semitonesPerOctave)] && keyRect(pitch).contains(point)) {
            return pitch;
        }
    }
    for (int i = 0; i < count; ++i) {
        const theory::Midi pitch = m_firstPitch + i;
        if (!isBlack[static_cast<std::size_t>(i % theory::semitonesPerOctave)] && keyRect(pitch).contains(point)) {
            return pitch;
        }
    }
    return -1;
}

void KeyboardStrip::mousePressEvent(QMouseEvent *event)
{
    const theory::Midi pitch = pitchAt(event->position());
    if (pitch >= 0) {
        emit pitchClicked(pitch);
    }
}

} // namespace cc::ui
