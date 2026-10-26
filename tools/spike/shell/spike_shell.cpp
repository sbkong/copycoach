// Spike (d): can the "Islands" look be isolated behind an interface so the shell can switch later?
//
// The shell is a plain QMainWindow with QDockWidgets. Everything that differs between the flat look and the
// JetBrains Islands look lives in ONE object, PanelChrome (gap between panels, corner radius, border), consumed by
// two places only: PanelFrame (paints each panel's background) and ShellStyle (dock separator width + colour).
// Panels, docks, layout code and painters never see the difference.
//
// Usage: spike_shell [--screenshot flat.png islands.png]   (screenshots both looks and quits)

#include <QApplication>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QPushButton>
#include <QStyleOption>
#include <QTimer>
#include <QToolBar>
#include <QStatusBar>
#include <QVBoxLayout>

#include <cstdio>
#include <string>

// Colours copied from docs/ui/tokens/theme-dark.json (Islands Dark). A real build reads the token file.
namespace color {
const QColor canvas("#26282C");  // background-canvas-color: window, gaps between islands
const QColor panel("#191A1C");   // background-layer-1-color
const QColor border("#40434A");  // border-color-emphasis (flat look only)
const QColor text("#D1D3D9");    // content-color-default
const QColor accent("#3871E1");  // accent-color-default
} // namespace color

// ---- the interface -------------------------------------------------------------------------------------------
class PanelChrome
{
public:
    virtual ~PanelChrome() = default;
    virtual int gap() const = 0;      // px between panels (also to the window edge)
    virtual int radius() const = 0;   // panel corner radius
    virtual bool border() const = 0;  // 1 px outline instead of a gap
    virtual const char *name() const = 0;
};

class FlatChrome : public PanelChrome
{
public:
    int gap() const override { return 0; }
    int radius() const override { return 0; }
    bool border() const override { return true; }
    const char *name() const override { return "flat"; }
};

// Values from ManyIslandsDark.theme.json: Island.arc 20, Island.borderWidth 6.
class IslandChrome : public PanelChrome
{
public:
    int gap() const override { return 6; }
    int radius() const override { return 20; }
    bool border() const override { return false; }
    const char *name() const override { return "islands"; }
};

// ---- consumer 1: the panel background ------------------------------------------------------------------------
class PanelFrame : public QWidget
{
public:
    explicit PanelFrame(const QString &title, const PanelChrome *chrome, QWidget *parent = nullptr)
        : QWidget(parent), m_chrome(chrome)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 8, 12, 8);
        auto *label = new QLabel(title);
        label->setStyleSheet("color:" + color::text.name() + "; font-size:14px;"); // spike only; real app uses QPalette
        layout->addWidget(label);
        layout->addStretch();
    }
    void setChrome(const PanelChrome *chrome)
    {
        m_chrome = chrome;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        if (m_chrome->radius() > 0) {
            QPainterPath path;
            path.addRoundedRect(r, m_chrome->radius(), m_chrome->radius());
            p.fillPath(path, color::panel);
        } else {
            p.fillRect(rect(), color::panel);
            if (m_chrome->border()) {
                p.setPen(color::border);
                p.drawRect(r);
            }
        }
    }

private:
    const PanelChrome *m_chrome;
};

// ---- consumer 2: the dock separators -------------------------------------------------------------------------
class ShellStyle : public QProxyStyle
{
public:
    explicit ShellStyle(const PanelChrome *chrome) : QProxyStyle("Fusion"), m_chrome(chrome) {}
    void setChrome(const PanelChrome *chrome) { m_chrome = chrome; }

    int pixelMetric(PixelMetric metric, const QStyleOption *opt, const QWidget *w) const override
    {
        if (metric == PM_DockWidgetSeparatorExtent) {
            return m_chrome->gap() > 0 ? m_chrome->gap() : 1;
        }
        return QProxyStyle::pixelMetric(metric, opt, w);
    }
    void drawPrimitive(PrimitiveElement el, const QStyleOption *opt, QPainter *p, const QWidget *w) const override
    {
        if (el == PE_IndicatorDockWidgetResizeHandle) {
            p->fillRect(opt->rect, m_chrome->gap() > 0 ? color::canvas : color::border);
            return;
        }
        QProxyStyle::drawPrimitive(el, opt, p, w);
    }

private:
    const PanelChrome *m_chrome;
};

// ---- the shell ----------------------------------------------------------------------------------------------
class Shell : public QMainWindow
{
public:
    Shell()
    {
        // One style object for the whole run; switching the look only swaps the chrome it reads.
        m_style = new ShellStyle(&m_flat);
        qApp->setStyle(m_style);
        setWindowTitle("spike_shell");
        resize(1280, 720);
        setDockOptions(AnimatedDocks | AllowNestedDocks | AllowTabbedDocks);
        // Spike only: the real app colours chrome through QPalette, not a stylesheet (PLAN 4-3).
        setStyleSheet("QMainWindow{background:" + color::canvas.name() + "} QToolBar{background:" + color::canvas.name()
                      + ";border:0} QStatusBar{background:" + color::canvas.name() + ";color:" + color::text.name() + "}");

        auto *bar = addToolBar("main");
        bar->setMovable(false);
        auto *toggle = new QPushButton("Islands 전환");
        connect(toggle, &QPushButton::clicked, this, [this] {
            const PanelChrome *next = (m_current == &m_island) ? static_cast<const PanelChrome *>(&m_flat) : &m_island;
            setChrome(next);
        });
        bar->addWidget(toggle);
        auto *maximize = new QPushButton("지판 패널 최대화 / 복원");
        connect(maximize, &QPushButton::clicked, this, &Shell::toggleMaximize);
        bar->addWidget(maximize);

        m_center = new PanelFrame("악보 / 탭 (중앙)", &m_flat);
        setCentralWidget(m_center);
        m_docks.push_back(addPanel("탐색", Qt::LeftDockWidgetArea, 220));
        m_docks.push_back(addPanel("인스펙터", Qt::RightDockWidgetArea, 240));
        m_fretboardDock = addPanel("인스트루먼트 패널 · 지판 + 건반 스트립", Qt::BottomDockWidgetArea, 0);
        m_docks.push_back(m_fretboardDock);
        statusBar()->showMessage("ASIO Focusrite USB · 48 kHz · 왕복 5.3 ms");
        setChrome(&m_flat);
    }

    void setChrome(const PanelChrome *chrome)
    {
        m_current = chrome;
        m_center->setChrome(chrome);
        for (auto *dock : m_docks) {
            static_cast<PanelFrame *>(dock->widget())->setChrome(chrome);
        }
        m_style->setChrome(chrome);
        // Make QMainWindow re-read PM_DockWidgetSeparatorExtent and relayout the docks.
        QEvent styleChange(QEvent::StyleChange);
        QApplication::sendEvent(this, &styleChange);
        const int g = chrome->gap();
        setContentsMargins(g, g, g, g); // the gap also runs along the window edge
        update();
        std::printf("chrome = %s (gap %d, radius %d)\n", chrome->name(), g, chrome->radius());
    }

    void toggleMaximize()
    {
        if (m_maximized) {
            restoreState(m_savedState);
            m_center->show();
            m_maximized = false;
            return;
        }
        m_savedState = saveState();
        for (auto *dock : m_docks) {
            if (dock != m_fretboardDock) dock->hide();
        }
        m_center->hide();
        m_maximized = true;
    }

    const PanelChrome *current() const { return m_current; }

private:
    QDockWidget *addPanel(const QString &title, Qt::DockWidgetArea area, int width)
    {
        auto *dock = new QDockWidget(title, this);
        dock->setObjectName(title);
        dock->setTitleBarWidget(new QWidget()); // the panel paints its own header; no native dock title bar
        dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
        auto *frame = new PanelFrame(title, &m_flat);
        if (width > 0) frame->setMinimumWidth(width);
        else frame->setMinimumHeight(200);
        dock->setWidget(frame);
        addDockWidget(area, dock);
        return dock;
    }

    FlatChrome m_flat;
    IslandChrome m_island;
    ShellStyle *m_style = nullptr; // owned by QApplication
    const PanelChrome *m_current = nullptr;
    PanelFrame *m_center = nullptr;
    QDockWidget *m_fretboardDock = nullptr;
    std::vector<QDockWidget *> m_docks;
    QByteArray m_savedState;
    bool m_maximized = false;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    Shell shell;
    shell.show();

    // Paths live at main scope and the lambdas copy them: an earlier version kept them in the if-block and
    // captured by reference, which dangled once app.exec() ran (heap corruption, 0xC0000374).
    const QString flatPng = argc >= 4 ? QString::fromLocal8Bit(argv[2]) : QString();
    const QString islandPng = argc >= 4 ? QString::fromLocal8Bit(argv[3]) : QString();
    const auto shot = [&shell](const QString &path, const char *label) {
        QImage img(shell.size(), QImage::Format_ARGB32);
        img.fill(color::canvas);
        shell.render(&img);
        std::printf("%s: %dx%d -> %s\n", label, img.width(), img.height(), img.save(path, "PNG") ? "ok" : "FAILED");
    };
    if (argc >= 4 && std::string(argv[1]) == "--screenshot") {
        QTimer::singleShot(400, [&shell, &app, shot, flatPng, islandPng] {
            shot(flatPng, "flat");
            shell.findChild<QPushButton *>()->click(); // "Islands 전환"
            QTimer::singleShot(400, [&shell, &app, shot, islandPng] {
                shot(islandPng, "islands");
                shell.toggleMaximize();
                QTimer::singleShot(400, [&app, shot, islandPng] {
                    shot(islandPng.chopped(4) + "-max.png", "islands+max");
                    app.quit();
                });
            });
        });
    }
    return app.exec();
}
