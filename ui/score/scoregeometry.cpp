// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/score/scoregeometry.h"

namespace cc::ui
{

std::optional<QRectF> ScoreGeometry::rectFor(const std::string &id) const
{
    const auto it = elements.find(id);
    if (it == elements.end() || it->second.logical.isNull()) {
        return std::nullopt;
    }
    return logicalToDevice.mapRect(it->second.logical);
}

std::string ScoreGeometry::elementAt(QPointF pagePoint, const QString &className) const
{
    const QPointF logical = logicalToDevice.inverted().map(pagePoint);
    std::string best;
    double bestArea = 0.0;
    for (const auto &[id, box] : elements) {
        if (box.className != className || !box.logical.contains(logical)) {
            continue;
        }
        const double area = box.logical.width() * box.logical.height();
        if (best.empty() || area < bestArea) {
            best = id;
            bestArea = area;
        }
    }
    return best;
}

} // namespace cc::ui
