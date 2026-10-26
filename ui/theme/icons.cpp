// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/theme/icons.h"

#include "ui/theme/theme.h"

#include <QFile>
#include <QIconEngine>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace cc::ui
{

namespace
{

using theme::Token;

QString resourcePath(const QString &name)
{
    return QStringLiteral(":/icons/lucide/") + name + QStringLiteral(".svg");
}

Token tokenFor(QIcon::Mode mode, QIcon::State state)
{
    if (mode == QIcon::Disabled) {
        return Token::ContentColorDisabled;
    }
    if (state == QIcon::On) {
        return Token::AccentContentColorOnAccent;
    }
    return Token::IconColorDefault;
}

/// Paints the SVG with the mode colour. The engine keeps the raw SVG; colour substitution happens per paint,
/// so a theme switch only needs the icon caches cleared (qpalette.md section 5).
class LucideIconEngine final : public QIconEngine
{
public:
    LucideIconEngine(QByteArray svg, const Theme *theme)
        : m_svg(std::move(svg))
        , m_theme(theme)
    {
    }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        QSvgRenderer renderer(coloured(mode, state));
        renderer.render(painter, rect);
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap pixmap(size * scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return pixmap;
    }

    [[nodiscard]] QIconEngine *clone() const override
    {
        return new LucideIconEngine(m_svg, m_theme); // NOLINT(cppcoreguidelines-owning-memory) QIcon takes it
    }

private:
    [[nodiscard]] QByteArray coloured(QIcon::Mode mode, QIcon::State state) const
    {
        QByteArray svg = m_svg;
        svg.replace("currentColor", m_theme->color(tokenFor(mode, state)).name(QColor::HexRgb).toLatin1());
        svg.replace("stroke-width=\"2\"", "stroke-width=\"1.5\"");
        return svg;
    }

    QByteArray m_svg;
    const Theme *m_theme;
};

} // namespace

Icons::Icons(const Theme &theme)
    : m_theme(&theme)
{
}

bool Icons::exists(const QString &name)
{
    return QFile::exists(resourcePath(name));
}

QIcon Icons::icon(const QString &name) const
{
    QFile file(resourcePath(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QIcon(new LucideIconEngine(file.readAll(), m_theme)); // NOLINT(cppcoreguidelines-owning-memory)
}

} // namespace cc::ui
