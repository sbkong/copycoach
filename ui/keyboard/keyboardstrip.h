// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/theory/constants.h"
#include "core/theory/pitch.h"
#include "ui/fretboard/fretboardview.h"

#include <QWidget>

#include <vector>

// Keyboard helper strip below the fretboard (ia.md section 2, components.md section 2.3): two octaves, the
// fretboard's notes marked in the same colour and shape, in pitch order. Click = sound.
namespace cc::ui
{

class Theme;

struct KeyMark {
    theory::Midi pitch = 60;
    QColor fill;
    DotShape shape = DotShape::Circle;
};

class KeyboardStrip final : public QWidget
{
    Q_OBJECT
public:
    static constexpr int octaves = 2;
    static constexpr int stripHeight = 56; // ia.md section 2

    explicit KeyboardStrip(const Theme &theme, QWidget *parent = nullptr);

    /// Marks to show; the strip scrolls so the lowest mark's octave is the first one.
    void setMarks(std::vector<KeyMark> marks);
    [[nodiscard]] QSize sizeHint() const override;

signals:
    void pitchClicked(int midi);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    [[nodiscard]] qreal whiteKeyWidth() const;
    [[nodiscard]] QRectF keyRect(theory::Midi pitch) const;
    [[nodiscard]] theory::Midi pitchAt(QPointF point) const;

    const Theme *m_theme;
    std::vector<KeyMark> m_marks;
    theory::Midi m_firstPitch = theory::middleC - theory::semitonesPerOctave; // C3
};

} // namespace cc::ui
