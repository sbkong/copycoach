// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/score/svgpath.h"

#include <array>
#include <cctype>
#include <charconv>
#include <optional>

namespace cc::ui
{

namespace
{

class PathParser
{
public:
    PathParser(std::string_view d, int &unsupportedArcs)
        : m_d(d)
        , m_arcs(&unsupportedArcs)
    {
        m_path.setFillRule(Qt::WindingFill);
    }

    QPainterPath parse()
    {
        while (nextCommand()) {
            if (!apply()) {
                break;
            }
            m_lastCommand = m_command;
        }
        return m_path;
    }

private:
    void skipSeparators()
    {
        while (
            m_pos < m_d.size()
            && (m_d[m_pos] == ' ' || m_d[m_pos] == ',' || std::isspace(static_cast<unsigned char>(m_d[m_pos])) != 0)) {
            ++m_pos;
        }
    }

    /// Reads the next command letter, or keeps the current one for a repeated coordinate group.
    bool nextCommand()
    {
        skipSeparators();
        if (m_pos >= m_d.size()) {
            return false;
        }
        const char c = m_d[m_pos];
        if (std::isalpha(static_cast<unsigned char>(c)) != 0) {
            m_command = c;
            ++m_pos;
        } else if (m_command == 0) {
            return false;
        } else if (m_command == 'M') {
            m_command = 'L'; // an implicit lineto follows a moveto
        } else if (m_command == 'm') {
            m_command = 'l';
        }
        return true;
    }

    std::optional<double> number()
    {
        skipSeparators();
        double value = 0.0;
        const auto *first = m_d.data() + m_pos; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) from_chars
        const auto *last = m_d.data() + m_d.size(); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        const auto [ptr, ec] = std::from_chars(first, last, value);
        if (ec != std::errc{} || ptr == first) {
            return std::nullopt;
        }
        m_pos += static_cast<std::size_t>(ptr - first);
        return value;
    }

    /// `count` numbers for the current command, or nullopt when the data runs out.
    std::optional<std::array<double, 7>> numbers(std::size_t count)
    {
        std::array<double, 7> values{};
        for (std::size_t i = 0; i < count; ++i) {
            const auto v = number();
            if (!v) {
                return std::nullopt;
            }
            values[i] = *v;
        }
        return values;
    }

    [[nodiscard]] QPointF origin() const
    {
        return std::islower(static_cast<unsigned char>(m_command)) != 0 ? m_current : QPointF(0, 0);
    }

    [[nodiscard]] bool lastWasCubic() const
    {
        return m_lastCommand == 'C' || m_lastCommand == 'c' || m_lastCommand == 'S' || m_lastCommand == 's';
    }

    [[nodiscard]] bool lastWasQuad() const
    {
        return m_lastCommand == 'Q' || m_lastCommand == 'q' || m_lastCommand == 'T' || m_lastCommand == 't';
    }

    bool apply()
    {
        switch (static_cast<char>(std::toupper(static_cast<unsigned char>(m_command)))) {
        case 'M':
            return moveOrLine(true);
        case 'L':
            return moveOrLine(false);
        case 'H':
            return axisLine(true);
        case 'V':
            return axisLine(false);
        case 'C':
            return cubic();
        case 'S':
            return smoothCubic();
        case 'Q':
            return quad();
        case 'T':
            return smoothQuad();
        case 'A':
            return arc();
        case 'Z':
            m_path.closeSubpath();
            m_current = m_start;
            return true;
        default:
            return false;
        }
    }

    bool moveOrLine(bool move)
    {
        const auto v = numbers(2);
        if (!v) {
            return false;
        }
        m_current = origin() + QPointF((*v)[0], (*v)[1]);
        if (move) {
            m_start = m_current;
            m_path.moveTo(m_current);
        } else {
            m_path.lineTo(m_current);
        }
        return true;
    }

    bool axisLine(bool horizontal)
    {
        const auto v = numbers(1);
        if (!v) {
            return false;
        }
        m_current = horizontal ? QPointF(origin().x() + (*v)[0], m_current.y())
                               : QPointF(m_current.x(), origin().y() + (*v)[0]);
        m_path.lineTo(m_current);
        return true;
    }

    bool cubic()
    {
        const auto v = numbers(6);
        if (!v) {
            return false;
        }
        const QPointF o = origin();
        const QPointF c1 = o + QPointF((*v)[0], (*v)[1]);
        const QPointF c2 = o + QPointF((*v)[2], (*v)[3]);
        m_current = o + QPointF((*v)[4], (*v)[5]);
        m_path.cubicTo(c1, c2, m_current);
        m_lastControl = c2;
        return true;
    }

    bool smoothCubic()
    {
        const auto v = numbers(4);
        if (!v) {
            return false;
        }
        const QPointF o = origin();
        const QPointF c1 = lastWasCubic() ? m_current + (m_current - m_lastControl) : m_current;
        const QPointF c2 = o + QPointF((*v)[0], (*v)[1]);
        m_current = o + QPointF((*v)[2], (*v)[3]);
        m_path.cubicTo(c1, c2, m_current);
        m_lastControl = c2;
        return true;
    }

    bool quad()
    {
        const auto v = numbers(4);
        if (!v) {
            return false;
        }
        const QPointF o = origin();
        const QPointF c1 = o + QPointF((*v)[0], (*v)[1]);
        m_current = o + QPointF((*v)[2], (*v)[3]);
        m_path.quadTo(c1, m_current);
        m_lastControl = c1;
        return true;
    }

    bool smoothQuad()
    {
        const auto v = numbers(2);
        if (!v) {
            return false;
        }
        const QPointF c1 = lastWasQuad() ? m_current + (m_current - m_lastControl) : m_current;
        m_current = origin() + QPointF((*v)[0], (*v)[1]);
        m_path.quadTo(c1, m_current);
        m_lastControl = c1;
        return true;
    }

    bool arc()
    {
        const auto v = numbers(7); // rx ry rotation large-arc sweep x y
        if (!v) {
            return false;
        }
        m_current = origin() + QPointF((*v)[5], (*v)[6]);
        m_path.lineTo(m_current);
        ++*m_arcs;
        return true;
    }

    std::string_view m_d;
    std::size_t m_pos = 0;
    int *m_arcs;
    QPainterPath m_path;
    QPointF m_current;
    QPointF m_start;
    QPointF m_lastControl;
    char m_command = 0;
    char m_lastCommand = 0;
};

} // namespace

QPainterPath parseSvgPath(std::string_view d, int &unsupportedArcs)
{
    return PathParser(d, unsupportedArcs).parse();
}

} // namespace cc::ui
