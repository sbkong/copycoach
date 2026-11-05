// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <vrv/devicecontext.h>

#include <QColor>
#include <QFont>
#include <QHash>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QTransform>

#include <map>
#include <string>
#include <vector>

class QPainter;

namespace vrv
{
class Glyph;
}

namespace cc::ui
{

/// Verovio DeviceContext over a QPainter (score-data-path.md section 5, spike (e)): the engraver calls the
/// primitives, they land on the painter in Verovio's logical units through one transform set in StartPage,
/// and every StartGraphic / EndGraphic pair records the union of what it drew under its xml:id. That union is
/// the geometry overlays and hit tests use. Music glyphs are painted as cached paths from the SMuFL font XML,
/// so no font file is installed; text goes through QFont ("Times" = the serif family of the theme).
class QtDeviceContext final : public vrv::DeviceContext
{
public:
    struct ElementBox {
        QString className; // Verovio class: "note", "tabGrp", "measure" ...
        QRectF logical; // union of everything drawn inside, logical units
    };

    explicit QtDeviceContext(QPainter *painter);
    ~QtDeviceContext() override = default;
    QtDeviceContext(const QtDeviceContext &) = delete;
    QtDeviceContext &operator=(const QtDeviceContext &) = delete;
    QtDeviceContext(QtDeviceContext &&) = delete;
    QtDeviceContext &operator=(QtDeviceContext &&) = delete;

    /// Ink color when Verovio names none (the theme's notation color); default black.
    void setDefaultColor(const QColor &color);
    void setSerifFamily(const QString &family);

    [[nodiscard]] QTransform logicalToDevice() const; // valid after StartPage
    [[nodiscard]] QSizeF pageSizePx() const; // valid after StartPage
    [[nodiscard]] const std::map<std::string, ElementBox> &elements() const;
    [[nodiscard]] int unsupportedArcCount() const;

    // vrv::DeviceContext. Signatures are Verovio's; the parameter-count rule does not apply to them.
    void SetBackground(int color, int style = vrv::PEN_SOLID) override;
    void SetBackgroundImage(void *image, double opacity = 1.0) override;
    void SetBackgroundMode(int mode) override;
    void SetTextForeground(int color) override;
    void SetTextBackground(int color) override;
    void SetLogicalOrigin(int x, int y) override;
    vrv::Point GetLogicalOrigin() override;

    // NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays) Verovio passes C arrays
    void DrawQuadBezierPath(vrv::Point bezier[3]) override;
    void DrawCubicBezierPath(vrv::Point bezier[4]) override;
    void DrawCubicBezierPathFilled(vrv::Point bezier1[4], vrv::Point bezier2[4]) override;
    void DrawBentParallelogramFilled(vrv::Point side[4], int height) override;
    void DrawCircle(int x, int y, int radius) override;
    void DrawEllipse(int x, int y, int width, int height) override;
    void DrawEllipticArc(int x, int y, int width, int height, double start, double end)
        override; // NOLINT(readability-function-size)
    void DrawLine(int x1, int y1, int x2, int y2) override;
    void DrawPolyline(int n, vrv::Point points[], bool close = false) override;
    void DrawPolygon(int n, vrv::Point points[]) override;
    // NOLINTEND(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    void DrawRectangle(int x, int y, int width, int height) override;
    void DrawRotatedText(const std::string &text, int x, int y, double angle) override;
    void DrawRoundedRectangle(int x, int y, int width, int height, int radius) override;
    void DrawText(const std::string &text, // NOLINT(readability-function-size)
                  const std::u32string &wtext = U"",
                  int x = VRV_UNSET,
                  int y = VRV_UNSET,
                  int width = VRV_UNSET,
                  int height = VRV_UNSET) override;
    void DrawMusicText(const std::u32string &text, int x, int y, bool setSmuflGlyph = false) override;
    void DrawSpline(int n,
                    vrv::Point points[]) override; // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    void DrawGraphicUri(int x, int y, int width, int height, const std::string &uri) override;
    void DrawSvgShape(int x, int y, int width, int height, double scale, pugi::xml_node svg)
        override; // NOLINT(readability-function-size)
    void DrawBackgroundImage(int x = 0, int y = 0) override;

    void StartText(int x, int y, vrv::data_HORIZONTALALIGNMENT alignment = vrv::HORIZONTALALIGNMENT_left) override;
    void EndText() override;
    void MoveTextTo(int x, int y, vrv::data_HORIZONTALALIGNMENT alignment) override;
    void MoveTextVerticallyTo(int y) override;

    void StartGraphic(vrv::Object *object,
                      const std::string &gClass,
                      const std::string &gId,
                      vrv::GraphicID graphicID = vrv::PRIMARY,
                      bool prepend = false) override;
    void EndGraphic(vrv::Object *object, vrv::View *view) override;
    void StartCustomGraphic(const std::string &name, std::string gClass = "", std::string gId = "") override;
    void EndCustomGraphic() override;
    void SetCustomGraphicColor(const std::string &color) override;
    void ResumeGraphic(vrv::Object *object, std::string gId) override;
    void EndResumedGraphic(vrv::Object *object, vrv::View *view) override;
    void RotateGraphic(vrv::Point const &orig, double angle) override;

    void StartPage() override;
    void EndPage() override;

private:
    struct Graphic {
        std::string id;
        QString className;
        QRectF bounds;
        bool hasColor = false; // pushed onto m_colorStack
        bool rotated = false; // painter.save() for RotateGraphic
    };
    /// A run of characters in one font inside a text chunk; smufl runs draw as glyph paths.
    struct TextRun {
        QFont font;
        QString text;
        std::u32string smufl;
        int pointSize = 0;
        double letterSpacing = 0.0;
        int dy = 0;
    };
    struct TextChunk {
        int x = 0;
        int y = 0;
        vrv::data_HORIZONTALALIGNMENT alignment = vrv::HORIZONTALALIGNMENT_left;
        std::vector<TextRun> runs;
    };

    [[nodiscard]] QColor currentColor() const;
    [[nodiscard]] QPen makePen() const;
    [[nodiscard]] QBrush makeBrush(bool fillByDefault) const;
    void track(const QRectF &logicalBounds);
    void strokePath(const QPainterPath &path);
    void fillAndStroke(const QPainterPath &path, bool fillByDefault);
    const QPainterPath &glyphPath(const vrv::Glyph *glyph);
    double glyphRunWidth(const std::u32string &text, int pointSize, double letterSpacing);
    double drawGlyphRun(const std::u32string &text, QPointF at, int pointSize, double letterSpacing);
    [[nodiscard]] double deviceScale() const;
    [[nodiscard]] QFont deviceFont(const TextRun &run) const;
    double textRunWidth(const TextRun &run);
    void drawTextRun(const TextRun &run, double x, double y);
    void flushTextChunk();
    void beginChunk(int x, int y, vrv::data_HORIZONTALALIGNMENT alignment);
    [[nodiscard]] TextRun
    makeRun(const vrv::FontInfo &font, const std::string &text, const std::u32string &wtext) const;

    QPainter *m_painter;
    QColor m_defaultColor{Qt::black};
    QString m_serifFamily{QStringLiteral("Times New Roman")};
    QTransform m_logicalToDevice;
    QSizeF m_pageSizePx;
    int m_originX = 0;
    int m_originY = 0;

    std::vector<Graphic> m_graphics;
    std::vector<QColor> m_colorStack;
    std::map<std::string, ElementBox> m_elements;

    QHash<const vrv::Glyph *, QPainterPath> m_glyphCache;
    int m_unsupportedArcs = 0;

    bool m_inText = false;
    TextChunk m_chunk;
    int m_textBaseY = 0;
};

} // namespace cc::ui
