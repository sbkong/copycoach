// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/theory/constants.h"
#include "core/theory/tuning.h"

#include <QColor>
#include <QString>
#include <QWidget>

#include <optional>
#include <vector>

// The fretboard painter (components.md section 2.3): frets 0..22, string 1 on top, dots = circle (chord tone),
// diamond (upper structure), dashed (next-slot preview), label always drawn. Reads tokens only through Theme.
namespace cc::ui
{

class Theme;

enum class DotShape : std::uint8_t {
    Circle,
    Diamond,
    Dashed,
};

enum class DotSize : std::uint8_t {
    Normal, // 24 px dot, 12 px label (ia.md section 2)
    Large, // 44 px dot, 18 px label: panel maximized, guitar in hand
};

struct FretDot {
    theory::StringFret position{1, 0};
    QString label;
    QColor fill;
    DotShape shape = DotShape::Circle;
    bool emphasized = false; // accent outline: the current chord's root / cursor note
};

struct FretboardScene {
    std::vector<FretDot> dots;
    QString caption; // top-left, e.g. "① Cmaj7 · 6th"
};

class FretboardView final : public QWidget
{
    Q_OBJECT
public:
    static constexpr int firstFret = 0;
    static constexpr int lastFret = 22;

    explicit FretboardView(const Theme &theme, QWidget *parent = nullptr);

    void setScene(FretboardScene scene);
    void setDotSize(DotSize size);
    void setStringCount(int count);
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

signals:
    /// Click in play mode: the position under the pointer (string 1..N, fret 0..22).
    void positionClicked(cc::theory::StringFret position);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    struct Geometry {
        qreal left, top, fretWidth, stringGap;
        [[nodiscard]] qreal fretX(int fret) const;
        [[nodiscard]] qreal stringY(int string) const;
    };
    [[nodiscard]] Geometry geometry() const;
    [[nodiscard]] std::optional<theory::StringFret> hitTest(QPointF point) const;
    void paintBoard(QPainter &painter, const Geometry &g) const;
    void paintDots(QPainter &painter, const Geometry &g) const;
    void paintDotShape(QPainter &painter, const FretDot &dot, const QRectF &box) const;
    [[nodiscard]] int dotRadius() const;
    [[nodiscard]] int labelPx() const;

    const Theme *m_theme;
    FretboardScene m_scene;
    DotSize m_dotSize = DotSize::Normal;
    int m_strings = 6;
};

} // namespace cc::ui
