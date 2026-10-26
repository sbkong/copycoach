// Spike (e): Verovio -> QPainter directly (QtDeviceContext) vs Verovio -> SVG -> QtSvg (spike b).
//
// Usage: spike_qtdc <file.musicxml|file.mei> --resources <verovio/data> --out <dir>
// Writes:
//   e-qtdc.png        page 1 drawn through QtDeviceContext at scale 40
//   e-svg.png         same page through RenderToSVG + post-processing + QSvgRenderer (spike b path)
//   e-side-by-side.png  both, left = QtDeviceContext, right = QtSvg
//   e-qtdc-boxes.png  QtDeviceContext render with red boxes from the recorded element geometry
//   e-qtdc-zoom.png   same page at 2.5x painter scale without re-engraving
// Prints timings, element counts, a geometry comparison against boundsOnElement, and a hit test.

#include "qt_device_context.h"

#include <vrv/toolkit.h>

#include <QGuiApplication>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

// Copied from spike_verovio.cpp: the two rewrites QtSvg needs.
static std::string flattenDefinitionScale(const std::string &svg)
{
    const size_t open = svg.find("<svg class=\"definition-scale\"");
    if (open == std::string::npos) return svg;
    const size_t openEnd = svg.find('>', open);
    const size_t close = svg.find("</svg>", openEnd);
    const size_t vb = svg.find("viewBox=\"", open);
    if (openEnd == std::string::npos || close == std::string::npos || vb == std::string::npos || vb > openEnd) {
        return svg;
    }
    double vbW = 0;
    double outerW = 0;
    std::sscanf(svg.c_str() + vb, "viewBox=\"%*f %*f %lf", &vbW);
    std::sscanf(svg.c_str() + svg.find("width=\""), "width=\"%lf", &outerW);
    if (vbW <= 0 || outerW <= 0) return svg;
    std::string attrs = svg.substr(open + 4, openEnd - open - 4);
    const size_t vbAttr = attrs.find(" viewBox=\"");
    attrs.erase(vbAttr, attrs.find('"', vbAttr + 10) - vbAttr + 1);
    char scale[64];
    std::snprintf(scale, sizeof scale, "<g transform=\"scale(%.6f)\"", outerW / vbW);
    return svg.substr(0, open) + scale + attrs + ">" + svg.substr(openEnd + 1, close - openEnd - 1) + "</g>"
        + svg.substr(close + 6);
}

static std::string unwrapNestedTspans(std::string svg)
{
    const std::string outerStart = "<tspan id=\"";
    size_t pos = 0;
    while ((pos = svg.find(outerStart, pos)) != std::string::npos) {
        const size_t outerEnd = svg.find('>', pos);
        const size_t inner = svg.find("<tspan ", outerEnd);
        const size_t innerText = svg.find('>', inner);
        const size_t innerClose = svg.find("</tspan>", innerText);
        const size_t outerClose = svg.find("</tspan>", innerClose + 8);
        if (inner == std::string::npos || innerClose == std::string::npos || outerClose == std::string::npos) break;
        const auto onlySpace = [&](size_t from, size_t to) { return svg.find_first_not_of(" \n\r\t", from) >= to; };
        if (!onlySpace(outerEnd + 1, inner) || !onlySpace(innerClose + 8, outerClose)) {
            pos = outerEnd;
            continue;
        }
        const std::string outerAttrs = svg.substr(pos + 6, outerEnd - pos - 6);
        const std::string innerAttrs = svg.substr(inner + 6, innerText - inner - 6);
        const std::string text = svg.substr(innerText + 1, innerClose - innerText - 1);
        const std::string merged = "<tspan" + outerAttrs + innerAttrs + ">" + text + "</tspan>";
        svg.replace(pos, outerClose + 8 - pos, merged);
        pos += merged.size();
    }
    return svg;
}

static double ms(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::printf("usage: spike_qtdc <file> --resources <dir> --out <dir>\n");
        return 1;
    }
    std::string resources;
    QString outDir = ".";
    for (int i = 2; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--resources") resources = argv[i + 1];
        if (std::string(argv[i]) == "--out") outDir = QString::fromUtf8(argv[i + 1]);
    }
    QGuiApplication app(argc, argv); // fonts need a QGuiApplication; no window is created

    QFile file(argv[1]);
    if (!file.open(QIODevice::ReadOnly)) {
        std::printf("cannot open %s\n", argv[1]);
        return 1;
    }
    const std::string data = file.readAll().toStdString();

    vrv::Toolkit toolkit(false);
    if (!resources.empty()) toolkit.SetResourcePath(resources);
    toolkit.SetOptions(R"({"scale": 40, "pageWidth": 2100, "pageHeight": 2970, "adjustPageHeight": true,
                           "breaks": "auto", "footer": "none", "header": "none", "xmlIdSeed": 1})");
    if (!toolkit.LoadData(data)) {
        std::printf("LoadData failed\n");
        return 2;
    }

    // --- 1. QtDeviceContext render -----------------------------------------------------------
    // Size is known only after StartPage; draw once into a throwaway image to learn it, then for real.
    QSizeF pageSize;
    {
        QImage probe(8, 8, QImage::Format_ARGB32);
        QPainter painter(&probe);
        QtDeviceContext dc(&painter);
        toolkit.RenderToDeviceContext(1, &dc);
        pageSize = dc.pageSizePx();
    }
    QImage qtdc(QSize(int(std::ceil(pageSize.width())), int(std::ceil(pageSize.height()))), QImage::Format_ARGB32);
    qtdc.fill(Qt::white);
    std::map<std::string, QtDeviceContext::ElementBox> elements;
    QTransform logicalToDevice;
    int arcs = 0;
    auto t0 = std::chrono::steady_clock::now();
    {
        QPainter painter(&qtdc);
        QtDeviceContext dc(&painter);
        toolkit.RenderToDeviceContext(1, &dc);
        elements = dc.elements();
        logicalToDevice = dc.logicalToDevice();
        arcs = dc.unsupportedArcCount();
    }
    auto t1 = std::chrono::steady_clock::now();

    // --- 2. SVG path (spike b) -----------------------------------------------------------------
    auto t2 = std::chrono::steady_clock::now();
    const std::string svg = toolkit.RenderToSVG(1);
    auto t3 = std::chrono::steady_clock::now();
    const std::string qtSvg = unwrapNestedTspans(flattenDefinitionScale(svg));
    QSvgRenderer renderer(QByteArray::fromStdString(qtSvg));
    auto t4 = std::chrono::steady_clock::now();
    QImage svgImage(renderer.defaultSize(), QImage::Format_ARGB32);
    svgImage.fill(Qt::white);
    {
        QPainter painter(&svgImage);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&painter);
    }
    auto t5 = std::chrono::steady_clock::now();

    std::printf("verovio %s  page px: qtdc %dx%d  svg %dx%d\n", toolkit.GetVersion().c_str(), qtdc.width(),
        qtdc.height(), svgImage.width(), svgImage.height());
    std::printf("QtDeviceContext: engrave+draw %.1f ms  (glyph arcs unsupported: %d)\n", ms(t0, t1), arcs);
    std::printf("SVG path:        RenderToSVG %.1f ms + parse %.1f ms + QtSvg draw %.1f ms = %.1f ms  (%zu bytes)\n",
        ms(t2, t3), ms(t3, t4), ms(t4, t5), ms(t2, t5), svg.size());

    // --- 3. zoom without re-engraving: the engraving is cached only as paint calls here, so this
    //        re-runs RenderToDeviceContext with a scaled painter (Verovio keeps the layout; no cast-off).
    auto t6 = std::chrono::steady_clock::now();
    QImage zoom(qtdc.size() * 2.5, QImage::Format_ARGB32);
    zoom.fill(Qt::white);
    {
        QPainter painter(&zoom);
        painter.scale(2.5, 2.5);
        QtDeviceContext dc(&painter);
        toolkit.RenderToDeviceContext(1, &dc);
    }
    auto t7 = std::chrono::steady_clock::now();
    std::printf("QtDeviceContext at 2.5x painter scale: %.1f ms (%dx%d)\n", ms(t6, t7), zoom.width(), zoom.height());

    // --- 4. geometry: recorded boxes vs QtSvg boundsOnElement for the same ids ------------------
    int notes = 0, tabGrps = 0, compared = 0, close = 0;
    double maxDelta = 0;
    QImage boxes = qtdc.copy();
    {
        QPainter painter(&boxes);
        painter.setPen(QPen(Qt::red, 1));
        for (const auto &[id, box] : elements) {
            if (box.className != "note" && box.className != "tabGrp") continue;
            if (box.className == "note") ++notes; else ++tabGrps;
            const QRectF px = logicalToDevice.mapRect(box.logical);
            painter.drawRect(px);
            const QString qid = QString::fromStdString(id);
            if (!renderer.elementExists(qid)) continue;
            const QRectF ref = renderer.transformForElement(qid).mapRect(renderer.boundsOnElement(qid));
            ++compared;
            const double delta = std::max({ std::abs(px.left() - ref.left()), std::abs(px.top() - ref.top()),
                std::abs(px.right() - ref.right()), std::abs(px.bottom() - ref.bottom()) });
            maxDelta = std::max(maxDelta, delta);
            if (delta <= 2.0) ++close;
        }
    }
    std::printf("elements recorded: %zu total, %d note, %d tabGrp\n", elements.size(), notes, tabGrps);
    std::printf("geometry vs QtSvg boundsOnElement: %d compared, %d within 2 px, max delta %.1f px\n", compared,
        close, maxDelta);

    // --- 5. hit test: the centre of the first note must resolve back to that note ---------------
    {
        std::string firstNote;
        QPointF centre;
        for (const auto &[id, box] : elements) {
            if (box.className == "note") {
                firstNote = id;
                centre = box.logical.center();
                break;
            }
        }
        std::string best;
        double bestArea = 0;
        for (const auto &[id, box] : elements) {
            if (box.className != "note" || !box.logical.contains(centre)) continue;
            const double area = box.logical.width() * box.logical.height();
            if (best.empty() || area < bestArea) { best = id; bestArea = area; }
        }
        std::printf("hit test: centre of %s -> %s (%s)\n", firstNote.c_str(), best.c_str(),
            best == firstNote ? "ok" : "MISMATCH");
    }

    // --- 6. side by side ---------------------------------------------------------------------------
    QImage side(qtdc.width() + svgImage.width() + 20, std::max(qtdc.height(), svgImage.height()) + 30,
        QImage::Format_ARGB32);
    side.fill(QColor(220, 220, 220));
    {
        QPainter painter(&side);
        painter.drawImage(0, 30, qtdc);
        painter.drawImage(qtdc.width() + 20, 30, svgImage);
        painter.setPen(Qt::black);
        painter.drawText(8, 20, "QtDeviceContext (direct QPainter)");
        painter.drawText(qtdc.width() + 28, 20, "RenderToSVG + QtSvg (spike b)");
    }

    const auto save = [&](const QImage &img, const char *name) {
        const QString path = outDir + "/" + name;
        std::printf("%s -> %s\n", name, img.save(path) ? "saved" : "SAVE FAILED");
    };
    save(qtdc, "e-qtdc.png");
    save(svgImage, "e-svg.png");
    save(side, "e-side-by-side.png");
    save(boxes, "e-qtdc-boxes.png");
    save(zoom, "e-qtdc-zoom.png");
    return 0;
}
