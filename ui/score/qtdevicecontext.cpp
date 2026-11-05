// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/score/qtdevicecontext.h"

#include "ui/score/svgpath.h"

#include <atts_shared.h>
#include <vrv/glyph.h>
#include <vrv/object.h>
#include <vrv/resources.h>
#include <vrv/view.h>

#include <QBrush>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cc::ui
{

namespace
{

constexpr unsigned byteMask = 0xffU;
constexpr unsigned redShift = 16;
constexpr unsigned greenShift = 8;

/// Applies a "scale(sx,sy)" transform attribute, the only one the glyph XML uses.
QPainterPath scaledBy(const QPainterPath &path, const QString &transform)
{
    if (!transform.startsWith(QStringLiteral("scale("))) {
        return path;
    }
    const QStringList parts = transform.mid(6, transform.indexOf(QLatin1Char(')')) - 6).split(QLatin1Char(','));
    const double sx = parts.value(0).toDouble();
    const double sy = parts.size() > 1 ? parts.value(1).toDouble() : sx;
    return QTransform::fromScale(sx, sy).map(path);
}

QColor colorFromInt(int color)
{
    const auto v = static_cast<unsigned>(color);
    return {static_cast<int>((v >> redShift) & byteMask),
            static_cast<int>((v >> greenShift) & byteMask),
            static_cast<int>(v & byteMask)};
}

QPointF pointOf(const vrv::Point &p)
{
    return {static_cast<double>(p.x), static_cast<double>(p.y)};
}

Qt::PenCapStyle capOf(int cap)
{
    switch (cap) {
    case vrv::LINECAP_ROUND:
        return Qt::RoundCap;
    case vrv::LINECAP_SQUARE:
        return Qt::SquareCap;
    default:
        return Qt::FlatCap; // SVG default
    }
}

Qt::PenJoinStyle joinOf(int join)
{
    switch (join) {
    case vrv::LINEJOIN_BEVEL:
        return Qt::BevelJoin;
    case vrv::LINEJOIN_ROUND:
        return Qt::RoundJoin;
    default:
        return Qt::MiterJoin; // SVG default
    }
}

} // namespace

QtDeviceContext::QtDeviceContext(QPainter *painter)
    : DeviceContext(vrv::DEVICE_CONTEXT)
    , m_painter(painter)
{
}

void QtDeviceContext::setDefaultColor(const QColor &color)
{
    m_defaultColor = color;
}

void QtDeviceContext::setSerifFamily(const QString &family)
{
    m_serifFamily = family;
}

QTransform QtDeviceContext::logicalToDevice() const
{
    return m_logicalToDevice;
}

QSizeF QtDeviceContext::pageSizePx() const
{
    return m_pageSizePx;
}

const std::map<std::string, QtDeviceContext::ElementBox> &QtDeviceContext::elements() const
{
    return m_elements;
}

int QtDeviceContext::unsupportedArcCount() const
{
    return m_unsupportedArcs;
}

// --- state --------------------------------------------------------------------------------------------------

void QtDeviceContext::SetBackground(int /*color*/, int /*style*/)
{
}
void QtDeviceContext::SetBackgroundImage(void * /*image*/, double /*opacity*/)
{
}
void QtDeviceContext::SetBackgroundMode(int /*mode*/)
{
}
void QtDeviceContext::SetTextBackground(int /*color*/)
{
}
void QtDeviceContext::DrawBackgroundImage(int /*x*/, int /*y*/)
{
}
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays) Verovio signature
void QtDeviceContext::DrawSpline(int /*n*/, vrv::Point /*points*/[])
{
}
void QtDeviceContext::DrawGraphicUri(int /*x*/, int /*y*/, int /*width*/, int /*height*/, const std::string & /*uri*/)
{
}
void QtDeviceContext::DrawSvgShape(int /*x*/,
                                   int /*y*/,
                                   int /*width*/,
                                   int /*height*/,
                                   double /*scale*/,
                                   pugi::xml_node /*svg*/)
{
} // NOLINT(readability-function-size)

void QtDeviceContext::SetTextForeground(int color)
{
    m_brushStack.top().SetColor(color); // SvgDeviceContext convention: brush color is text color
}

void QtDeviceContext::SetLogicalOrigin(int x, int y)
{
    m_originX = -x;
    m_originY = -y;
}

vrv::Point QtDeviceContext::GetLogicalOrigin()
{
    return {m_originX, m_originY};
}

QColor QtDeviceContext::currentColor() const
{
    return m_colorStack.empty() ? m_defaultColor : m_colorStack.back();
}

QPen QtDeviceContext::makePen() const
{
    const vrv::Pen &pen = m_penStack.top();
    if (pen.GetWidth() <= 0) {
        return {Qt::NoPen}; // SVG would draw a 1-unit hairline: invisible at any scale
    }
    QColor color = pen.HasColor() ? colorFromInt(pen.GetColor()) : currentColor();
    if (pen.HasOpacity()) {
        color.setAlphaF(pen.GetOpacity());
    }
    QPen result(color, pen.GetWidth());
    result.setCapStyle(capOf(pen.GetLineCap()));
    result.setJoinStyle(joinOf(pen.GetLineJoin()));
    if (pen.GetDashLength() > 0) {
        const double width = pen.GetWidth();
        const double gap = pen.GetGapLength() > 0 ? pen.GetGapLength() : pen.GetDashLength();
        result.setDashPattern({pen.GetDashLength() / width, gap / width});
    }
    return result;
}

QBrush QtDeviceContext::makeBrush(bool fillByDefault) const
{
    const vrv::Brush &brush = m_brushStack.top();
    if (!brush.HasColor() && !fillByDefault) {
        return {Qt::NoBrush};
    }
    QColor color = brush.HasColor() ? colorFromInt(brush.GetColor()) : currentColor();
    if (brush.HasOpacity()) {
        color.setAlphaF(brush.GetOpacity());
    }
    return {color};
}

void QtDeviceContext::track(const QRectF &logicalBounds)
{
    if (m_isDeactivatedX && m_isDeactivatedY) {
        return;
    }
    for (Graphic &g : m_graphics) {
        g.bounds |= logicalBounds;
    }
}

void QtDeviceContext::strokePath(const QPainterPath &path)
{
    m_painter->setPen(makePen());
    m_painter->setBrush(Qt::NoBrush);
    m_painter->drawPath(path);
    track(path.boundingRect());
}

void QtDeviceContext::fillAndStroke(const QPainterPath &path, bool fillByDefault)
{
    m_painter->setPen(makePen());
    m_painter->setBrush(makeBrush(fillByDefault));
    m_painter->drawPath(path);
    track(path.boundingRect());
}

// --- shapes (fill / stroke conventions of SvgDeviceContext) -------------------------------------------------

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays) Verovio passes C arrays
void QtDeviceContext::DrawQuadBezierPath(vrv::Point bezier[3])
{
    QPainterPath path(pointOf(bezier[0])); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) Verovio array
    path.quadTo(pointOf(bezier[1]), pointOf(bezier[2])); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    strokePath(path);
}

void QtDeviceContext::DrawCubicBezierPath(vrv::Point bezier[4])
{
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic) Verovio array
    QPainterPath path(pointOf(bezier[0]));
    path.cubicTo(pointOf(bezier[1]), pointOf(bezier[2]), pointOf(bezier[3]));
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    strokePath(path);
}

void QtDeviceContext::DrawCubicBezierPathFilled(vrv::Point bezier1[4], vrv::Point bezier2[4])
{
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic) Verovio arrays
    QPainterPath path(pointOf(bezier1[0]));
    path.cubicTo(pointOf(bezier1[1]), pointOf(bezier1[2]), pointOf(bezier1[3]));
    path.cubicTo(pointOf(bezier2[2]), pointOf(bezier2[1]), pointOf(bezier2[0]));
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    fillAndStroke(path, true);
}

void QtDeviceContext::DrawBentParallelogramFilled(vrv::Point side[4], int height)
{
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic) Verovio arrays
    QPainterPath path(pointOf(side[0]));
    path.cubicTo(pointOf(side[1]), pointOf(side[2]), pointOf(side[3]));
    path.lineTo(pointOf(side[3]) + QPointF(0, height));
    path.cubicTo(pointOf(side[2]) + QPointF(0, height),
                 pointOf(side[1]) + QPointF(0, height),
                 pointOf(side[0]) + QPointF(0, height));
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    path.closeSubpath();
    fillAndStroke(path, true);
}
// NOLINTEND(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)

void QtDeviceContext::DrawCircle(int x, int y, int radius)
{
    DrawEllipse(x - radius, y - radius, 2 * radius, 2 * radius);
}

void QtDeviceContext::DrawEllipse(int x, int y, int width, int height)
{
    QPainterPath path;
    path.addEllipse(QRectF(x, y, width, height));
    fillAndStroke(path, true);
}

// NOLINTNEXTLINE(readability-function-size) Verovio signature
void QtDeviceContext::DrawEllipticArc(int x, int y, int width, int height, double start, double end)
{
    // Verovio angles are degrees counter-clockwise from 3 o'clock, like QPainterPath::arcTo.
    QPainterPath path;
    path.arcMoveTo(QRectF(x, y, width, height), start);
    path.arcTo(QRectF(x, y, width, height), start, end - start);
    fillAndStroke(path, true);
}

void QtDeviceContext::DrawLine(int x1, int y1, int x2, int y2)
{
    QPainterPath path(QPointF(x1, y1));
    path.lineTo(QPointF(x2, y2));
    strokePath(path);
}

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays) Verovio passes C arrays
void QtDeviceContext::DrawPolyline(int n, vrv::Point points[], bool close)
{
    if (n <= 0) {
        return;
    }
    QPainterPath path(pointOf(points[0])); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) Verovio array
    for (int i = 1; i < n; ++i) {
        path.lineTo(pointOf(points[i])); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }
    if (close) {
        path.closeSubpath();
    }
    strokePath(path);
}

void QtDeviceContext::DrawPolygon(int n, vrv::Point points[])
{
    if (n <= 0) {
        return;
    }
    QPainterPath path(pointOf(points[0])); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) Verovio array
    for (int i = 1; i < n; ++i) {
        path.lineTo(pointOf(points[i])); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }
    path.closeSubpath();
    path.setFillRule(Qt::WindingFill);
    fillAndStroke(path, true);
}
// NOLINTEND(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)

void QtDeviceContext::DrawRectangle(int x, int y, int width, int height)
{
    DrawRoundedRectangle(x, y, width, height, 0);
}

void QtDeviceContext::DrawRoundedRectangle(int x, int y, int width, int height, int radius)
{
    QPainterPath path;
    const QRectF rect = QRectF(x, y, width, height).normalized();
    if (radius > 0) {
        path.addRoundedRect(rect, radius, radius);
    } else {
        path.addRect(rect);
    }
    fillAndStroke(path, true);
}

/// Text at an angle (SvgDeviceContext leaves this unimplemented): one run, rotated about its anchor.
void QtDeviceContext::DrawRotatedText(const std::string &text, int x, int y, double angle)
{
    if (m_fontStack.empty() || text.empty()) {
        return;
    }
    const TextRun run = makeRun(*m_fontStack.top(), text, {});
    m_painter->save();
    m_painter->translate(x, y);
    m_painter->rotate(angle);
    drawTextRun(run, 0.0, 0.0);
    m_painter->restore();
}

// --- music glyphs -------------------------------------------------------------------------------------------

const QPainterPath &QtDeviceContext::glyphPath(const vrv::Glyph *glyph)
{
    if (const auto it = m_glyphCache.find(glyph); it != m_glyphCache.end()) {
        return it.value();
    }
    // Glyph XML: <g id="E0A4"><path transform="scale(1,-1)" d="..."/>...</g>, font units with y up.
    QPainterPath combined;
    combined.setFillRule(Qt::WindingFill);
    pugi::xml_document doc;
    if (doc.load_string(glyph->GetXML().c_str())) {
        for (const pugi::xml_node &child : doc.first_child().children("path")) {
            const QPainterPath path = parseSvgPath(child.attribute("d").value(), m_unsupportedArcs);
            combined.addPath(scaledBy(path, QString::fromUtf8(child.attribute("transform").value())));
        }
    }
    return *m_glyphCache.insert(glyph, combined);
}

double QtDeviceContext::glyphRunWidth(const std::u32string &text, int pointSize, double letterSpacing)
{
    const vrv::Resources *resources = this->GetResources();
    if (resources == nullptr) {
        return 0.0;
    }
    double width = 0.0;
    for (const char32_t c : text) {
        const vrv::Glyph *glyph = resources->GetGlyph(c);
        if (glyph == nullptr) {
            continue;
        }
        int gx = 0;
        int gy = 0;
        int w = 0;
        int h = 0;
        glyph->GetBoundingBox(gx, gy, w, h);
        const int advance = glyph->GetHorizAdvX() > 0 ? glyph->GetHorizAdvX() : w;
        width += (static_cast<double>(advance) * pointSize / glyph->GetUnitsPerEm()) + letterSpacing;
    }
    return width;
}

double QtDeviceContext::drawGlyphRun(const std::u32string &text, QPointF at, int pointSize, double letterSpacing)
{
    const vrv::Resources *resources = this->GetResources();
    if (resources == nullptr) {
        return 0.0;
    }
    const QColor color = currentColor();
    double x = at.x();
    for (const char32_t c : text) {
        const vrv::Glyph *glyph = resources->GetGlyph(c);
        if (glyph == nullptr) {
            continue;
        }
        const double scale = static_cast<double>(pointSize) / glyph->GetUnitsPerEm() * DEFINITION_FACTOR;
        QTransform local;
        local.translate(x, at.y());
        local.scale(scale, scale);
        const QPainterPath &path = glyphPath(glyph);
        m_painter->setPen(Qt::NoPen);
        m_painter->setBrush(color);
        m_painter->drawPath(local.map(path));
        track(local.mapRect(path.boundingRect()));
        x += glyphRunWidth(std::u32string(1, c), pointSize, letterSpacing);
    }
    return x - at.x();
}

void QtDeviceContext::DrawMusicText(const std::u32string &text, int x, int y, bool /*setSmuflGlyph*/)
{
    const vrv::FontInfo *font = m_fontStack.top();
    drawGlyphRun(text, QPointF(x, y), font->GetPointSize(), 0.0);
}

// --- text ---------------------------------------------------------------------------------------------------

void QtDeviceContext::beginChunk(int x, int y, vrv::data_HORIZONTALALIGNMENT alignment)
{
    const vrv::data_HORIZONTALALIGNMENT keep = m_chunk.alignment;
    flushTextChunk();
    m_chunk = TextChunk{x, y, alignment != vrv::HORIZONTALALIGNMENT_NONE ? alignment : keep, {}};
    m_textBaseY = y;
}

void QtDeviceContext::StartText(int x, int y, vrv::data_HORIZONTALALIGNMENT alignment)
{
    beginChunk(x, y, alignment);
    m_inText = true;
}

void QtDeviceContext::MoveTextTo(int x, int y, vrv::data_HORIZONTALALIGNMENT alignment)
{
    beginChunk(x, y, alignment);
}

void QtDeviceContext::MoveTextVerticallyTo(int y)
{
    m_textBaseY = y;
}

void QtDeviceContext::EndText()
{
    flushTextChunk();
    m_inText = false;
}

QtDeviceContext::TextRun
QtDeviceContext::makeRun(const vrv::FontInfo &font, const std::string &text, const std::u32string &wtext) const
{
    TextRun run;
    run.pointSize = font.GetPointSize();
    run.letterSpacing = font.GetLetterSpacing();
    run.dy = m_textBaseY - m_chunk.y;
    if (font.GetSmuflFont() != vrv::SMUFL_NONE) {
        run.smufl = wtext.empty() ? std::u32string(text.begin(), text.end()) : wtext;
        return run;
    }
    run.text = QString::fromStdString(text);
    QString family = QString::fromStdString(font.GetFaceName());
    if (family.isEmpty() || family == QStringLiteral("Times")) {
        family = m_serifFamily; // Verovio lays text out with Times metrics; the theme picks the serif face
    }
    run.font = QFont(family);
    run.font.setPixelSize(std::max(1, font.GetPointSize()));
    run.font.setItalic(font.GetStyle() == vrv::FONTSTYLE_italic || font.GetStyle() == vrv::FONTSTYLE_oblique);
    run.font.setBold(font.GetWeight() == vrv::FONTWEIGHT_bold);
    run.font.setHintingPreference(QFont::PreferNoHinting);
    if (run.letterSpacing != 0.0) {
        run.font.setLetterSpacing(QFont::AbsoluteSpacing, run.letterSpacing);
    }
    return run;
}

// NOLINTNEXTLINE(readability-function-size) Verovio signature
void QtDeviceContext::DrawText(const std::string &text,
                               const std::u32string &wtext,
                               int x,
                               int y,
                               int width,
                               int height)
{
    if (m_fontStack.empty()) {
        return;
    }
    const bool hasPos = x != 0 && y != 0 && x != VRV_UNSET && y != VRV_UNSET;
    const bool hasBox = hasPos && width != 0 && height != 0 && width != VRV_UNSET && height != VRV_UNSET;
    if (hasPos && !hasBox) {
        beginChunk(x, y, vrv::HORIZONTALALIGNMENT_NONE); // an explicit tspan position opens a new chunk
        m_inText = true;
    }
    if (text.empty() && wtext.empty()) {
        return;
    }
    m_chunk.runs.push_back(makeRun(*m_fontStack.top(), text, wtext));
    if (!m_inText) {
        flushTextChunk(); // DrawText outside StartText / EndText draws at once
    }
}

/// Uniform scale of the painter's world transform (logical units -> device pixels).
double QtDeviceContext::deviceScale() const
{
    const QTransform world = m_painter->worldTransform();
    return std::sqrt(std::abs(world.determinant()));
}

/// Qt fonts are sized in device pixels: a font of thousands of logical units does not rasterize on every
/// platform (the offscreen one draws nothing), so text is laid out in device space and mapped back.
QFont QtDeviceContext::deviceFont(const TextRun &run) const
{
    QFont font = run.font;
    font.setPixelSize(std::max(1, static_cast<int>(std::lround(run.pointSize * deviceScale()))));
    if (run.letterSpacing != 0.0) {
        font.setLetterSpacing(QFont::AbsoluteSpacing, run.letterSpacing * deviceScale());
    }
    return font;
}

double QtDeviceContext::textRunWidth(const TextRun &run)
{
    if (!run.smufl.empty()) {
        return glyphRunWidth(run.smufl, run.pointSize, run.letterSpacing);
    }
    return QFontMetricsF(deviceFont(run)).horizontalAdvance(run.text) / deviceScale();
}

void QtDeviceContext::drawTextRun(const TextRun &run, double x, double y)
{
    if (!run.smufl.empty()) {
        drawGlyphRun(run.smufl, QPointF(x, y), run.pointSize, run.letterSpacing);
        return;
    }
    const QTransform world = m_painter->worldTransform();
    const double scale = deviceScale();
    const QFont font = deviceFont(run);
    const QFontMetricsF metrics(font);
    m_painter->save();
    m_painter->resetTransform();
    m_painter->setFont(font);
    m_painter->setPen(QPen(currentColor()));
    m_painter->setBrush(Qt::NoBrush);
    m_painter->drawText(world.map(QPointF(x, y)), run.text);
    m_painter->restore();
    const double width = metrics.horizontalAdvance(run.text) / scale;
    track(QRectF(x, y - (metrics.ascent() / scale), width, (metrics.ascent() + metrics.descent()) / scale));
}

void QtDeviceContext::flushTextChunk()
{
    if (m_chunk.runs.empty()) {
        return;
    }
    double total = 0.0;
    for (const TextRun &run : m_chunk.runs) {
        total += textRunWidth(run);
    }
    double x = m_chunk.x;
    if (m_chunk.alignment == vrv::HORIZONTALALIGNMENT_center) {
        x -= total / 2;
    } else if (m_chunk.alignment == vrv::HORIZONTALALIGNMENT_right) {
        x -= total;
    }
    for (const TextRun &run : m_chunk.runs) {
        drawTextRun(run, x, m_chunk.y + run.dy);
        x += textRunWidth(run);
    }
    m_chunk.runs.clear();
}

// --- graphics (element ids) ---------------------------------------------------------------------------------

void QtDeviceContext::StartGraphic(vrv::Object *object,
                                   const std::string & /*gClass*/,
                                   const std::string &gId,
                                   vrv::GraphicID /*graphicID*/,
                                   bool /*prepend*/)
{
    Graphic g;
    g.id = gId;
    g.className = QString::fromStdString(object->GetClassName());
    if (object->HasAttClass(vrv::ATT_COLOR)) {
        if (const auto *att = dynamic_cast<const vrv::AttColor *>(object); att != nullptr && att->HasColor()) {
            m_colorStack.emplace_back(QString::fromStdString(att->GetColor()));
            g.hasColor = true;
        }
    }
    m_graphics.push_back(g);
}

/// Pending text is flushed first so its bounds land in the graphic that owns it (spike (e) item 1).
void QtDeviceContext::EndGraphic(vrv::Object * /*object*/, vrv::View * /*view*/)
{
    if (m_graphics.empty()) {
        return;
    }
    flushTextChunk();
    const Graphic g = m_graphics.back();
    m_graphics.pop_back();
    if (g.rotated) {
        m_painter->restore();
    }
    if (g.hasColor) {
        m_colorStack.pop_back();
    }
    if (!g.id.empty()) {
        ElementBox &box = m_elements[g.id];
        if (box.className.isEmpty()) {
            box.className = g.className;
        }
        box.logical |= g.bounds;
    }
}

void QtDeviceContext::StartCustomGraphic(const std::string &name, std::string /*gClass*/, std::string gId)
{
    Graphic g;
    g.id = std::move(gId);
    g.className = QString::fromStdString(name);
    m_graphics.push_back(g);
}

void QtDeviceContext::EndCustomGraphic()
{
    EndGraphic(nullptr, nullptr);
}

void QtDeviceContext::SetCustomGraphicColor(const std::string &color)
{
    if (m_graphics.empty() || m_graphics.back().hasColor) {
        return;
    }
    m_colorStack.emplace_back(QString::fromStdString(color));
    m_graphics.back().hasColor = true;
}

void QtDeviceContext::ResumeGraphic(vrv::Object * /*object*/, std::string gId)
{
    Graphic g;
    g.id = std::move(gId);
    m_graphics.push_back(g);
}

void QtDeviceContext::EndResumedGraphic(vrv::Object * /*object*/, vrv::View * /*view*/)
{
    EndGraphic(nullptr, nullptr);
}

void QtDeviceContext::RotateGraphic(vrv::Point const &orig, double angle)
{
    if (m_graphics.empty() || m_graphics.back().rotated) {
        return;
    }
    m_painter->save();
    m_painter->translate(orig.x, orig.y);
    m_painter->rotate(angle);
    m_painter->translate(-orig.x, -orig.y);
    m_graphics.back().rotated = true;
}

// --- page ---------------------------------------------------------------------------------------------------

/// Same mapping as SvgDeviceContext: the page is GetWidth() * userScale px wide and shows GetWidth() *
/// viewBoxFactor logical units, with the margin translate applied inside.
void QtDeviceContext::StartPage()
{
    const double scale = this->GetUserScaleX() / this->GetViewBoxFactor();
    m_pageSizePx = QSizeF(this->GetWidth() * this->GetUserScaleX(), this->GetContentHeight() * this->GetUserScaleY());
    QTransform t;
    t.scale(scale, scale);
    t.translate(m_originX, m_originY);
    m_logicalToDevice = t;
    m_painter->save();
    m_painter->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    m_painter->setWorldTransform(t, true);
    m_elements.clear();
    m_graphics.clear();
    m_colorStack.clear();
}

void QtDeviceContext::EndPage()
{
    flushTextChunk();
    m_painter->restore();
}

} // namespace cc::ui
