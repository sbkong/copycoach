// Spike (e): a Verovio DeviceContext that draws straight into a QPainter.
//
// Verovio engraves and calls the DeviceContext for every primitive (lines, beziers, glyphs, text).
// SvgDeviceContext turns those calls into SVG; this class turns them into QPainter calls, so no SVG
// string, no QtSvg, and no post-processing sit between the engraver and the screen.
//
// Coordinates arrive in Verovio's logical units (y already flipped, positive down). The painter is
// given one transform (logical -> pixels) in StartPage, so every primitive is drawn in logical units.
//
// While drawing, every StartGraphic/EndGraphic pair records the union of the primitives drawn inside
// it, keyed by the MEI xml:id. That is the geometry an overlay (cursor, playhead, bend arrows) and a
// hit test need, and it comes for free from the drawing pass.

#pragma once

#include <vrv/devicecontext.h>

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

namespace vrv {
class Glyph;
}

class QtDeviceContext : public vrv::DeviceContext {
public:
    struct ElementBox {
        QString className; // Verovio class name: "note", "tabGrp", "measure", ...
        QRectF logical;    // union of everything drawn inside the graphic, in Verovio logical units
    };

    explicit QtDeviceContext(QPainter *painter);
    ~QtDeviceContext() override;

    // Transform from logical units to the painter's device, valid after StartPage.
    QTransform logicalToDevice() const { return m_logicalToDevice; }

    // Page size in pixels at the user scale, valid after StartPage.
    QSizeF pageSizePx() const { return m_pageSizePx; }

    // Geometry recorded during drawing (xml:id -> box). Boxes are in logical units; map them with
    // logicalToDevice() for pixels.
    const std::map<std::string, ElementBox> &elements() const { return m_elements; }

    // Smallest recorded element of the given class that contains the point (logical units).
    // Returns an empty string when nothing matches.
    std::string elementAt(const QPointF &logical, const QString &className) const;

    int unsupportedArcCount() const { return m_unsupportedArcs; }

    // --- vrv::DeviceContext ------------------------------------------------------------------
    void SetBackground(int color, int style = vrv::PEN_SOLID) override;
    void SetBackgroundImage(void *image, double opacity = 1.0) override;
    void SetBackgroundMode(int mode) override;
    void SetTextForeground(int color) override;
    void SetTextBackground(int color) override;
    void SetLogicalOrigin(int x, int y) override;
    vrv::Point GetLogicalOrigin() override;

    void DrawQuadBezierPath(vrv::Point bezier[3]) override;
    void DrawCubicBezierPath(vrv::Point bezier[4]) override;
    void DrawCubicBezierPathFilled(vrv::Point bezier1[4], vrv::Point bezier2[4]) override;
    void DrawBentParallelogramFilled(vrv::Point side[4], int height) override;
    void DrawCircle(int x, int y, int radius) override;
    void DrawEllipse(int x, int y, int width, int height) override;
    void DrawEllipticArc(int x, int y, int width, int height, double start, double end) override;
    void DrawLine(int x1, int y1, int x2, int y2) override;
    void DrawPolyline(int n, vrv::Point points[], bool close = false) override;
    void DrawPolygon(int n, vrv::Point points[]) override;
    void DrawRectangle(int x, int y, int width, int height) override;
    void DrawRotatedText(const std::string &text, int x, int y, double angle) override;
    void DrawRoundedRectangle(int x, int y, int width, int height, int radius) override;
    void DrawText(const std::string &text, const std::u32string &wtext = U"", int x = VRV_UNSET, int y = VRV_UNSET,
        int width = VRV_UNSET, int height = VRV_UNSET) override;
    void DrawMusicText(const std::u32string &text, int x, int y, bool setSmuflGlyph = false) override;
    void DrawSpline(int n, vrv::Point points[]) override;
    void DrawGraphicUri(int x, int y, int width, int height, const std::string &uri) override;
    void DrawSvgShape(int x, int y, int width, int height, double scale, pugi::xml_node svg) override;
    void DrawBackgroundImage(int x = 0, int y = 0) override;

    void StartText(int x, int y, vrv::data_HORIZONTALALIGNMENT alignment = vrv::HORIZONTALALIGNMENT_left) override;
    void EndText() override;
    void MoveTextTo(int x, int y, vrv::data_HORIZONTALALIGNMENT alignment) override;
    void MoveTextVerticallyTo(int y) override;

    void StartGraphic(vrv::Object *object, const std::string &gClass, const std::string &gId,
        vrv::GraphicID graphicID = vrv::PRIMARY, bool prepend = false) override;
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
        bool hasColor = false; // pushed a color onto m_colorStack
        bool rotated = false;  // painter.save() was called for RotateGraphic
    };

    // One piece of a text block: a run of characters in one font, with an optional baseline shift.
    struct TextRun {
        QFont font;
        QString text;
        std::u32string smufl; // non-empty: draw with music glyph paths instead of a Qt font
        int pointSize = 0;
        double letterSpacing = 0;
        int dy = 0;
    };
    struct TextChunk {
        int x = 0;
        int y = 0;
        vrv::data_HORIZONTALALIGNMENT alignment = vrv::HORIZONTALALIGNMENT_left;
        std::vector<TextRun> runs;
    };

    QColor currentColor() const;
    QPen makePen() const;
    QBrush makeBrush(bool fillByDefault) const;
    void track(const QRectF &logicalBounds);
    void strokePath(const QPainterPath &path);
    void fillAndStroke(const QPainterPath &path, bool fillByDefault);
    const QPainterPath &glyphPath(const vrv::Glyph *glyph);
    double drawGlyphRun(const std::u32string &text, double x, double y, int pointSize, double letterSpacing,
        bool measureOnly);
    double textRunWidth(const TextRun &run);
    void flushTextChunk();

    QPainter *m_painter;
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
