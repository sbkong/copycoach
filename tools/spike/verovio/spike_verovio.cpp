// Spike (b): Verovio MusicXML -> SVG rendered in a Qt Widgets window, plus timemap extraction.
//
// Usage: spike_verovio <file.musicxml|file.mei> [--svg out.svg] [--resources <verovio/data>] [--screenshot out.png]
// Checks: load time, render time, SVG size, timemap entries, and that QtSvg displays the result.
// --screenshot grabs the window after it is shown and quits, so the test also runs on a headless session.

#include <vrv/toolkit.h>

#include <QApplication>
#include <QFile>
#include <QGraphicsScene>
#include <QGraphicsSvgItem>
#include <QGraphicsView>
#include <QImage>
#include <QMainWindow>
#include <QPainter>
#include <QSvgRenderer>
#include <QTimer>

#include <chrono>
#include <cstdio>
#include <string>

// Turns Verovio's inner <svg class="definition-scale" ... viewBox="0 0 W H"> into <g transform="scale(s)" ...>,
// where s maps the viewBox width onto the outer width in px. Everything else in the SVG stays as it is.
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

    // Keep the other attributes (color, font-family) and drop the viewBox.
    std::string attrs = svg.substr(open + 4, openEnd - open - 4); // ' class="definition-scale" color=... viewBox=...'
    const size_t vbAttr = attrs.find(" viewBox=\"");
    attrs.erase(vbAttr, attrs.find('"', vbAttr + 10) - vbAttr + 1);

    char scale[64];
    std::snprintf(scale, sizeof scale, "<g transform=\"scale(%.6f)\"", outerW / vbW);
    return svg.substr(0, open) + scale + attrs + ">" + svg.substr(openEnd + 1, close - openEnd - 1) + "</g>"
        + svg.substr(close + 6);
}

// QtSvg also skips text inside a <tspan> nested in another <tspan>. Verovio emits labels and <dir> text as
// <tspan id=".." class="text"><tspan font-size="..">H</tspan></tspan>, so merge the two levels into one.
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
        // Only touch the simple case: outer tspan directly wrapping one inner tspan, nothing else in between.
        const auto onlySpace = [&](size_t from, size_t to) {
            return svg.find_first_not_of(" \n\r\t", from) >= to;
        };
        if (!onlySpace(outerEnd + 1, inner) || !onlySpace(innerClose + 8, outerClose)) {
            pos = outerEnd;
            continue;
        }
        const std::string outerAttrs = svg.substr(pos + 6, outerEnd - pos - 6);       // ' id=".." class=".."'
        const std::string innerAttrs = svg.substr(inner + 6, innerText - inner - 6);   // ' font-size=".."'
        const std::string text = svg.substr(innerText + 1, innerClose - innerText - 1);
        const std::string merged = "<tspan" + outerAttrs + innerAttrs + ">" + text + "</tspan>";
        svg.replace(pos, outerClose + 8 - pos, merged);
        pos += merged.size();
    }
    return svg;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::printf("usage: spike_verovio <file.musicxml> [--svg out.svg] [--resources <verovio/data>]\n");
        return 1;
    }
    std::string svgOut;
    std::string resources;
    std::string screenshot;
    for (int i = 2; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--svg") {
            svgOut = argv[i + 1];
        } else if (std::string(argv[i]) == "--resources") {
            resources = argv[i + 1];
        } else if (std::string(argv[i]) == "--screenshot") {
            screenshot = argv[i + 1];
        }
    }

    QApplication app(argc, argv);

    // "--render-svg in.svg out.png": rasterize an arbitrary SVG with QtSvg, to test which SVG features it accepts.
    if (std::string(argv[1]) == "--render-svg" && argc >= 4) {
        QSvgRenderer renderer(QString::fromUtf8(argv[2]));
        QImage image(renderer.defaultSize().isEmpty() ? QSize(840, 250) : renderer.defaultSize(), QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        renderer.render(&painter);
        painter.end();
        // Count non-white pixels so a blank result is visible in the console output.
        long inked = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (image.pixel(x, y) != 0xffffffff) ++inked;
            }
        }
        std::printf("%s: valid=%d size=%dx%d inked=%ld -> %s\n", argv[2], renderer.isValid(), image.width(),
                    image.height(), inked, image.save(QString::fromUtf8(argv[3])) ? "saved" : "SAVE FAILED");
        return 0;
    }

    QFile file(argv[1]);
    if (!file.open(QIODevice::ReadOnly)) {
        std::printf("cannot open %s\n", argv[1]);
        return 1;
    }
    const std::string data = file.readAll().toStdString();

    vrv::Toolkit toolkit(false); // do not auto-load resources from the default path
    if (!resources.empty()) {
        toolkit.SetResourcePath(resources);
    }
    toolkit.SetOptions(R"({"scale": 40, "pageWidth": 2100, "pageHeight": 2970, "adjustPageHeight": true,
                           "breaks": "auto", "footer": "none", "header": "none"})");

    auto t0 = std::chrono::steady_clock::now();
    if (!toolkit.LoadData(data)) {
        std::printf("LoadData failed\n");
        return 2;
    }
    auto t1 = std::chrono::steady_clock::now();
    const std::string svg = toolkit.RenderToSVG(1);
    auto t2 = std::chrono::steady_clock::now();
    const std::string timemap = toolkit.RenderToTimemap();
    auto t3 = std::chrono::steady_clock::now();

    const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    std::printf("verovio %s  pages=%d  load=%.1fms  svg=%.1fms (%zu bytes)  timemap=%.1fms (%zu bytes)\n",
                toolkit.GetVersion().c_str(), toolkit.GetPageCount(), ms(t0, t1), ms(t1, t2), svg.size(), ms(t2, t3),
                timemap.size());

    if (!svgOut.empty()) {
        QFile out(QString::fromStdString(svgOut));
        if (out.open(QIODevice::WriteOnly)) {
            out.write(svg.data(), static_cast<qint64>(svg.size()));
        }
    }

    // QtSvg draws nothing inside a nested <svg viewBox="..."> element, and Verovio always wraps the page in
    // <svg class="definition-scale" viewBox="0 0 W H">. Replace that one element with a scaled <g>.
    const std::string qtSvg = unwrapNestedTspans(flattenDefinitionScale(svg));

    auto *renderer = new QSvgRenderer(QByteArray::fromStdString(qtSvg));
    std::printf("QtSvg valid=%d  size=%dx%d\n", renderer->isValid(), renderer->defaultSize().width(),
                renderer->defaultSize().height());

    // Overlay plan check: can we locate Verovio elements by their MEI xml:id inside the rendered SVG?
    for (const char *cls : { "class=\"note\"", "class=\"tabGrp\"", "class=\"staff\"" }) {
        const size_t pos = qtSvg.find(cls);
        if (pos == std::string::npos) continue;
        const size_t idStart = qtSvg.rfind("id=\"", pos) + 4;
        const QString id = QString::fromStdString(qtSvg.substr(idStart, qtSvg.find('"', idStart) - idStart));
        const QRectF b = renderer->boundsOnElement(id);
        std::printf("boundsOnElement(%s %s) = (%.1f, %.1f) %.1fx%.1f\n", cls, id.toStdString().c_str(), b.x(), b.y(),
                    b.width(), b.height());
    }

    if (!screenshot.empty()) {
        // Render through QSvgRenderer alone, without any window, to tell QtSvg problems from window ones.
        // Then draw a red box around every note and tabGrp, mapped with transformForElement: this is the
        // overlay mechanism planned for bends and hammer-on arcs.
        QImage image(renderer->defaultSize(), QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        renderer->render(&painter);
        painter.setPen(QPen(Qt::red, 1));
        for (const char *cls : { "class=\"note\"", "class=\"tabGrp\"" }) {
            size_t pos = 0;
            while ((pos = qtSvg.find(cls, pos)) != std::string::npos) {
                const size_t idStart = qtSvg.rfind("id=\"", pos) + 4;
                const QString id = QString::fromStdString(qtSvg.substr(idStart, qtSvg.find('"', idStart) - idStart));
                painter.drawRect(renderer->transformForElement(id).mapRect(renderer->boundsOnElement(id)));
                pos += 1;
            }
        }
        painter.end();
        const QString direct = QString::fromStdString(screenshot).replace(".png", "-direct.png");
        std::printf("direct render %s -> %s\n", direct.toStdString().c_str(), image.save(direct) ? "ok" : "FAILED");
    }

    auto *item = new QGraphicsSvgItem();
    item->setSharedRenderer(renderer);
    auto *scene = new QGraphicsScene();
    scene->addItem(item);
    const QRectF bounds = item->boundingRect();
    std::printf("item bounds %.0fx%.0f  viewBox %dx%d\n", bounds.width(), bounds.height(), renderer->viewBox().width(),
                renderer->viewBox().height());
    auto *view = new QGraphicsView(scene);
    view->setRenderHint(QPainter::Antialiasing);

    QMainWindow window;
    window.setCentralWidget(view);
    window.resize(1280, 800);
    window.setWindowTitle("spike_verovio");
    window.show();

    if (!screenshot.empty()) {
        QTimer::singleShot(500, [&] {
            const bool ok = window.grab().save(QString::fromStdString(screenshot));
            std::printf("screenshot %s -> %s\n", screenshot.c_str(), ok ? "ok" : "FAILED");
            app.quit();
        });
    }
    return app.exec();
}
