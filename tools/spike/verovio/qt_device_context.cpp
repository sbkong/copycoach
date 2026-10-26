#include "qt_device_context.h"

#include <vrv/glyph.h>
#include <vrv/object.h>
#include <vrv/resources.h>
#include <vrv/view.h>
#include "atts_shared.h"

#include <QBrush>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>

#include <cmath>
#include <cstdlib>

using namespace vrv;

namespace {

QColor colorFromInt(int color)
{
    return QColor((color >> 16) & 0xff, (color >> 8) & 0xff, color & 0xff);
}

// Parses an SVG path "d" string into a QPainterPath. Font glyph outlines use M/L/C/Q/Z and the
// relative forms; H/V/S/T are handled too. Arcs (A) are replaced by a straight line and counted.
QPainterPath parseSvgPath(const QString &d, int *unsupportedArcs)
{
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    const QByteArray bytes = d.toLatin1();
    const char *p = bytes.constData();
    const char *end = p + bytes.size();

    char cmd = 0;
    QPointF cur, start, lastCtrl;
    char lastCmd = 0;

    auto skipSep = [&] {
        while (p < end && (*p == ' ' || *p == ',' || *p == '\n' || *p == '\t' || *p == '\r')) ++p;
    };
    auto number = [&](double &out) {
        skipSep();
        if (p >= end) return false;
        char *next = nullptr;
        out = std::strtod(p, &next);
        if (next == p) return false;
        p = next;
        return true;
    };
    auto hasNumber = [&] {
        skipSep();
        return p < end && (*p == '-' || *p == '+' || *p == '.' || (*p >= '0' && *p <= '9'));
    };

    while (true) {
        skipSep();
        if (p >= end) break;
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
            cmd = *p++;
        } else if (cmd == 0) {
            break;
        } else if (cmd == 'M') {
            cmd = 'L'; // implicit lineto after moveto
        } else if (cmd == 'm') {
            cmd = 'l';
        }

        const bool rel = (cmd >= 'a' && cmd <= 'z');
        const QPointF origin = rel ? cur : QPointF(0, 0);
        double a = 0, b = 0, c = 0, dd = 0, e = 0, f = 0;

        switch (cmd) {
            case 'M': case 'm':
                if (!number(a) || !number(b)) return path;
                cur = origin + QPointF(a, b);
                start = cur;
                path.moveTo(cur);
                break;
            case 'L': case 'l':
                if (!number(a) || !number(b)) return path;
                cur = origin + QPointF(a, b);
                path.lineTo(cur);
                break;
            case 'H': case 'h':
                if (!number(a)) return path;
                cur = QPointF(origin.x() + a, cur.y());
                path.lineTo(cur);
                break;
            case 'V': case 'v':
                if (!number(a)) return path;
                cur = QPointF(cur.x(), origin.y() + a);
                path.lineTo(cur);
                break;
            case 'C': case 'c': {
                if (!number(a) || !number(b) || !number(c) || !number(dd) || !number(e) || !number(f)) return path;
                const QPointF c1 = origin + QPointF(a, b);
                const QPointF c2 = origin + QPointF(c, dd);
                cur = origin + QPointF(e, f);
                path.cubicTo(c1, c2, cur);
                lastCtrl = c2;
                break;
            }
            case 'S': case 's': {
                if (!number(a) || !number(b) || !number(c) || !number(dd)) return path;
                const bool reflect = (lastCmd == 'C' || lastCmd == 'c' || lastCmd == 'S' || lastCmd == 's');
                const QPointF c1 = reflect ? cur + (cur - lastCtrl) : cur;
                const QPointF c2 = origin + QPointF(a, b);
                cur = origin + QPointF(c, dd);
                path.cubicTo(c1, c2, cur);
                lastCtrl = c2;
                break;
            }
            case 'Q': case 'q': {
                if (!number(a) || !number(b) || !number(c) || !number(dd)) return path;
                const QPointF c1 = origin + QPointF(a, b);
                cur = origin + QPointF(c, dd);
                path.quadTo(c1, cur);
                lastCtrl = c1;
                break;
            }
            case 'T': case 't': {
                if (!number(a) || !number(b)) return path;
                const bool reflect = (lastCmd == 'Q' || lastCmd == 'q' || lastCmd == 'T' || lastCmd == 't');
                const QPointF c1 = reflect ? cur + (cur - lastCtrl) : cur;
                cur = origin + QPointF(a, b);
                path.quadTo(c1, cur);
                lastCtrl = c1;
                break;
            }
            case 'A': case 'a':
                if (!number(a) || !number(b) || !number(c) || !number(dd) || !number(e) || !number(f)) return path;
                if (!number(a)) return path; // 7th value: end y (the 6th read above is end x)
                cur = origin + QPointF(f, a);
                path.lineTo(cur);
                if (unsupportedArcs) ++*unsupportedArcs;
                break;
            case 'Z': case 'z':
                path.closeSubpath();
                cur = start;
                break;
            default:
                return path;
        }
        lastCmd = cmd;
        // Repeated coordinate groups reuse the command; the loop reads the next letter otherwise.
    }
    (void)hasNumber;
    return path;
}

} // namespace

QtDeviceContext::QtDeviceContext(QPainter *painter) : DeviceContext(DEVICE_CONTEXT), m_painter(painter) {}

QtDeviceContext::~QtDeviceContext() {}

// --- state ----------------------------------------------------------------------------------

void QtDeviceContext::SetBackground(int, int) {}
void QtDeviceContext::SetBackgroundImage(void *, double) {}
void QtDeviceContext::SetBackgroundMode(int) {}
void QtDeviceContext::SetTextBackground(int) {}
void QtDeviceContext::DrawBackgroundImage(int, int) {}

void QtDeviceContext::SetTextForeground(int color)
{
    m_brushStack.top().SetColor(color); // same convention as SvgDeviceContext: brush color is text color
}

void QtDeviceContext::SetLogicalOrigin(int x, int y)
{
    m_originX = -x;
    m_originY = -y;
}

Point QtDeviceContext::GetLogicalOrigin()
{
    return Point(m_originX, m_originY);
}

QColor QtDeviceContext::currentColor() const
{
    return m_colorStack.empty() ? QColor(Qt::black) : m_colorStack.back();
}

QPen QtDeviceContext::makePen() const
{
    const Pen &pen = m_penStack.top();
    if (pen.GetWidth() <= 0) return QPen(Qt::NoPen); // SVG would draw a 1-unit hairline: invisible at any scale
    QColor color = pen.HasColor() ? colorFromInt(pen.GetColor()) : currentColor();
    if (pen.HasOpacity()) color.setAlphaF(pen.GetOpacity());
    QPen qpen(color, pen.GetWidth());
    switch (pen.GetLineCap()) {
        case LINECAP_BUTT: qpen.setCapStyle(Qt::FlatCap); break;
        case LINECAP_ROUND: qpen.setCapStyle(Qt::RoundCap); break;
        case LINECAP_SQUARE: qpen.setCapStyle(Qt::SquareCap); break;
        default: qpen.setCapStyle(Qt::FlatCap); break; // SVG default
    }
    switch (pen.GetLineJoin()) {
        case LINEJOIN_BEVEL: qpen.setJoinStyle(Qt::BevelJoin); break;
        case LINEJOIN_ROUND: qpen.setJoinStyle(Qt::RoundJoin); break;
        default: qpen.setJoinStyle(Qt::MiterJoin); break; // SVG default
    }
    if (pen.GetDashLength() > 0) {
        const double w = pen.GetWidth();
        const double gap = (pen.GetGapLength() > 0) ? pen.GetGapLength() : pen.GetDashLength();
        qpen.setDashPattern({ pen.GetDashLength() / w, gap / w });
    }
    return qpen;
}

QBrush QtDeviceContext::makeBrush(bool fillByDefault) const
{
    const Brush &brush = m_brushStack.top();
    if (!brush.HasColor() && !fillByDefault) return QBrush(Qt::NoBrush);
    QColor color = brush.HasColor() ? colorFromInt(brush.GetColor()) : currentColor();
    if (brush.HasOpacity()) color.setAlphaF(brush.GetOpacity());
    return QBrush(color);
}

void QtDeviceContext::track(const QRectF &logicalBounds)
{
    if (m_isDeactivatedX && m_isDeactivatedY) return;
    for (Graphic &g : m_graphics) g.bounds |= logicalBounds;
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

// --- shapes (same fill/stroke conventions as SvgDeviceContext) ----------------------------------

void QtDeviceContext::DrawQuadBezierPath(Point b[3])
{
    QPainterPath path(QPointF(b[0].x, b[0].y));
    path.quadTo(QPointF(b[1].x, b[1].y), QPointF(b[2].x, b[2].y));
    strokePath(path);
}

void QtDeviceContext::DrawCubicBezierPath(Point b[4])
{
    QPainterPath path(QPointF(b[0].x, b[0].y));
    path.cubicTo(QPointF(b[1].x, b[1].y), QPointF(b[2].x, b[2].y), QPointF(b[3].x, b[3].y));
    strokePath(path);
}

void QtDeviceContext::DrawCubicBezierPathFilled(Point b1[4], Point b2[4])
{
    QPainterPath path(QPointF(b1[0].x, b1[0].y));
    path.cubicTo(QPointF(b1[1].x, b1[1].y), QPointF(b1[2].x, b1[2].y), QPointF(b1[3].x, b1[3].y));
    path.cubicTo(QPointF(b2[2].x, b2[2].y), QPointF(b2[1].x, b2[1].y), QPointF(b2[0].x, b2[0].y));
    fillAndStroke(path, true);
}

void QtDeviceContext::DrawBentParallelogramFilled(Point s[4], int height)
{
    QPainterPath path(QPointF(s[0].x, s[0].y));
    path.cubicTo(QPointF(s[1].x, s[1].y), QPointF(s[2].x, s[2].y), QPointF(s[3].x, s[3].y));
    path.lineTo(QPointF(s[3].x, s[3].y + height));
    path.cubicTo(QPointF(s[2].x, s[2].y + height), QPointF(s[1].x, s[1].y + height), QPointF(s[0].x, s[0].y + height));
    path.closeSubpath();
    fillAndStroke(path, true);
}

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

void QtDeviceContext::DrawPolyline(int n, Point points[], bool close)
{
    if (n <= 0) return;
    QPainterPath path(QPointF(points[0].x, points[0].y));
    for (int i = 1; i < n; ++i) path.lineTo(QPointF(points[i].x, points[i].y));
    if (close) path.closeSubpath();
    strokePath(path);
}

void QtDeviceContext::DrawPolygon(int n, Point points[])
{
    if (n <= 0) return;
    QPainterPath path(QPointF(points[0].x, points[0].y));
    for (int i = 1; i < n; ++i) path.lineTo(QPointF(points[i].x, points[i].y));
    path.closeSubpath();
    path.setFillRule(Qt::WindingFill);
    fillAndStroke(path, true);
}

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

void QtDeviceContext::DrawRotatedText(const std::string &, int, int, double) {} // SvgDeviceContext: TODO as well
void QtDeviceContext::DrawSpline(int, Point[]) {}
void QtDeviceContext::DrawGraphicUri(int, int, int, int, const std::string &) {}
void QtDeviceContext::DrawSvgShape(int, int, int, int, double, pugi::xml_node) {}

// --- music glyphs ------------------------------------------------------------------------------

const QPainterPath &QtDeviceContext::glyphPath(const Glyph *glyph)
{
    auto it = m_glyphCache.find(glyph);
    if (it != m_glyphCache.end()) return it.value();

    // Glyph XML: <g id="E0A4"><path transform="scale(1,-1)" d="..."/>...</g>  (font units, y up)
    QPainterPath combined;
    combined.setFillRule(Qt::WindingFill);
    pugi::xml_document doc;
    if (doc.load_string(glyph->GetXML().c_str())) {
        pugi::xml_node g = doc.first_child();
        for (pugi::xml_node child = g.first_child(); child; child = child.next_sibling()) {
            if (std::string(child.name()) != "path") continue;
            QPainterPath path = parseSvgPath(QString::fromUtf8(child.attribute("d").value()), &m_unsupportedArcs);
            const QString transform = QString::fromUtf8(child.attribute("transform").value());
            if (transform.startsWith("scale(")) {
                const QStringList parts = transform.mid(6, transform.indexOf(')') - 6).split(',');
                const double sx = parts.value(0).toDouble();
                const double sy = parts.size() > 1 ? parts.value(1).toDouble() : sx;
                path = QTransform::fromScale(sx, sy).map(path);
            }
            combined.addPath(path);
        }
    }
    return *m_glyphCache.insert(glyph, combined);
}

double QtDeviceContext::drawGlyphRun(
    const std::u32string &text, double x, double y, int pointSize, double letterSpacing, bool measureOnly)
{
    const Resources *resources = this->GetResources();
    if (!resources) return 0;
    const double startX = x;
    const QColor color = currentColor();
    for (char32_t c : text) {
        const Glyph *glyph = resources->GetGlyph(c);
        if (!glyph) continue;
        const double scale = (double)pointSize / glyph->GetUnitsPerEm() * DEFINITION_FACTOR;
        if (!measureOnly) {
            const QPainterPath &path = glyphPath(glyph);
            QTransform local;
            local.translate(x, y);
            local.scale(scale, scale);
            m_painter->setPen(Qt::NoPen);
            m_painter->setBrush(color);
            m_painter->drawPath(local.map(path));
            track(local.mapRect(path.boundingRect()));
        }
        int gx, gy, w, h;
        if (glyph->GetHorizAdvX() > 0) {
            x += (double)glyph->GetHorizAdvX() * pointSize / glyph->GetUnitsPerEm();
        } else {
            glyph->GetBoundingBox(gx, gy, w, h);
            x += (double)w * pointSize / glyph->GetUnitsPerEm();
        }
        x += letterSpacing;
    }
    return x - startX;
}

void QtDeviceContext::DrawMusicText(const std::u32string &text, int x, int y, bool)
{
    const FontInfo *font = m_fontStack.top();
    // SvgDeviceContext applies widthToHeightRatio to the x scale only; ignored here (always 1.0 in practice).
    drawGlyphRun(text, x, y, font->GetPointSize(), 0, false);
}

// --- text ----------------------------------------------------------------------------------------

void QtDeviceContext::StartText(int x, int y, data_HORIZONTALALIGNMENT alignment)
{
    flushTextChunk();
    m_inText = true;
    m_chunk = TextChunk{ x, y, alignment, {} };
    m_textBaseY = y;
}

void QtDeviceContext::MoveTextTo(int x, int y, data_HORIZONTALALIGNMENT alignment)
{
    const data_HORIZONTALALIGNMENT keep = m_chunk.alignment;
    flushTextChunk();
    m_chunk = TextChunk{ x, y, (alignment != HORIZONTALALIGNMENT_NONE) ? alignment : keep, {} };
    m_textBaseY = y;
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

void QtDeviceContext::DrawText(const std::string &text, const std::u32string &wtext, int x, int y, int width, int height)
{
    if (!m_fontStack.size()) return;
    const bool hasPos = (x != 0) && (y != 0) && (x != VRV_UNSET) && (y != VRV_UNSET);
    const bool hasBox = hasPos && (width != 0) && (height != 0) && (width != VRV_UNSET) && (height != VRV_UNSET);
    if (hasPos && !hasBox) {
        // An explicit position on a tspan starts a new text chunk (SVG semantics).
        const data_HORIZONTALALIGNMENT keep = m_chunk.alignment;
        flushTextChunk();
        m_chunk = TextChunk{ x, y, keep, {} };
        m_textBaseY = y;
        m_inText = true;
    }
    if (text.empty() && wtext.empty()) return;

    const FontInfo *font = m_fontStack.top();
    TextRun run;
    run.pointSize = font->GetPointSize();
    run.letterSpacing = font->GetLetterSpacing();
    run.dy = m_textBaseY - m_chunk.y;
    if (font->GetSmuflFont() != SMUFL_NONE) {
        run.smufl = wtext.empty() ? std::u32string(text.begin(), text.end()) : wtext;
    } else {
        run.text = QString::fromStdString(text);
        QString family = QString::fromStdString(font->GetFaceName());
        if (family.isEmpty() || family == "Times") family = "Times New Roman";
        run.font = QFont(family);
        run.font.setPixelSize(std::max(1, font->GetPointSize()));
        run.font.setItalic(font->GetStyle() == FONTSTYLE_italic || font->GetStyle() == FONTSTYLE_oblique);
        run.font.setBold(font->GetWeight() == FONTWEIGHT_bold);
        run.font.setHintingPreference(QFont::PreferNoHinting);
        if (run.letterSpacing != 0) run.font.setLetterSpacing(QFont::AbsoluteSpacing, run.letterSpacing);
    }
    m_chunk.runs.push_back(run);
    if (!m_inText) {
        flushTextChunk(); // DrawText outside StartText/EndText: draw immediately
    }
}

double QtDeviceContext::textRunWidth(const TextRun &run)
{
    if (!run.smufl.empty()) return drawGlyphRun(run.smufl, 0, 0, run.pointSize, run.letterSpacing, true);
    return QFontMetricsF(run.font).horizontalAdvance(run.text);
}

void QtDeviceContext::flushTextChunk()
{
    if (m_chunk.runs.empty()) return;
    double total = 0;
    for (const TextRun &run : m_chunk.runs) total += textRunWidth(run);
    double x = m_chunk.x;
    if (m_chunk.alignment == HORIZONTALALIGNMENT_center) x -= total / 2;
    if (m_chunk.alignment == HORIZONTALALIGNMENT_right) x -= total;

    for (const TextRun &run : m_chunk.runs) {
        const double y = m_chunk.y + run.dy;
        if (!run.smufl.empty()) {
            x += drawGlyphRun(run.smufl, x, y, run.pointSize, run.letterSpacing, false);
            continue;
        }
        m_painter->setFont(run.font);
        m_painter->setPen(QPen(currentColor()));
        m_painter->setBrush(Qt::NoBrush);
        m_painter->drawText(QPointF(x, y), run.text);
        const QFontMetricsF metrics(run.font);
        const double w = metrics.horizontalAdvance(run.text);
        track(QRectF(x, y - metrics.ascent(), w, metrics.ascent() + metrics.descent()));
        x += w;
    }
    m_chunk.runs.clear();
}

// --- graphics (element ids) ------------------------------------------------------------------------

void QtDeviceContext::StartGraphic(Object *object, const std::string &, const std::string &gId, GraphicID, bool)
{
    Graphic g;
    g.id = gId;
    g.className = QString::fromStdString(object->GetClassName());
    if (object->HasAttClass(ATT_COLOR)) {
        if (auto *att = dynamic_cast<AttColor *>(object); att && att->HasColor()) {
            m_colorStack.push_back(QColor(QString::fromStdString(att->GetColor())));
            g.hasColor = true;
        }
    }
    m_graphics.push_back(g);
}

void QtDeviceContext::EndGraphic(Object *, View *)
{
    if (m_graphics.empty()) return;
    const Graphic g = m_graphics.back();
    m_graphics.pop_back();
    if (g.rotated) m_painter->restore();
    if (g.hasColor) m_colorStack.pop_back();
    if (!g.id.empty()) {
        ElementBox &box = m_elements[g.id];
        if (box.className.isEmpty()) box.className = g.className;
        box.logical |= g.bounds;
    }
}

void QtDeviceContext::StartCustomGraphic(const std::string &name, std::string, std::string gId)
{
    Graphic g;
    g.id = gId;
    g.className = QString::fromStdString(name);
    m_graphics.push_back(g);
}

void QtDeviceContext::EndCustomGraphic()
{
    EndGraphic(nullptr, nullptr);
}

void QtDeviceContext::SetCustomGraphicColor(const std::string &color)
{
    if (m_graphics.empty() || m_graphics.back().hasColor) return;
    m_colorStack.push_back(QColor(QString::fromStdString(color)));
    m_graphics.back().hasColor = true;
}

void QtDeviceContext::ResumeGraphic(Object *, std::string gId)
{
    Graphic g;
    g.id = gId;
    m_graphics.push_back(g);
}

void QtDeviceContext::EndResumedGraphic(Object *, View *)
{
    EndGraphic(nullptr, nullptr);
}

void QtDeviceContext::RotateGraphic(Point const &orig, double angle)
{
    if (m_graphics.empty() || m_graphics.back().rotated) return;
    m_painter->save();
    m_painter->translate(orig.x, orig.y);
    m_painter->rotate(angle);
    m_painter->translate(-orig.x, -orig.y);
    m_graphics.back().rotated = true;
}

// --- page ------------------------------------------------------------------------------------------

void QtDeviceContext::StartPage()
{
    // Same mapping as SvgDeviceContext: the page is GetWidth()*userScale px wide and shows a viewBox of
    // GetWidth()*viewBoxFactor logical units, with the margin translate applied inside the viewBox.
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

std::string QtDeviceContext::elementAt(const QPointF &logical, const QString &className) const
{
    std::string best;
    double bestArea = 0;
    for (const auto &[id, box] : m_elements) {
        if (box.className != className || !box.logical.contains(logical)) continue;
        const double area = box.logical.width() * box.logical.height();
        if (best.empty() || area < bestArea) {
            best = id;
            bestArea = area;
        }
    }
    return best;
}
