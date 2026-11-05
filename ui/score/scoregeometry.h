// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "ui/score/qtdevicecontext.h"

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTransform>

#include <map>
#include <optional>
#include <string>

namespace cc::ui
{

/// What one rendered page knows about its elements (score-data-path.md section 5): xml:id -> box. Overlays
/// ask it for rectangles by id, hit tests ask it for the id under a point; which id belongs to which tick is
/// the model's business, so nothing here depends on the renderer.
struct ScoreGeometry {
    std::map<std::string, QtDeviceContext::ElementBox> elements;
    QTransform logicalToDevice; // logical units -> page pixels

    /// The element's box in page pixels, if it was drawn on this page.
    [[nodiscard]] std::optional<QRectF> rectFor(const std::string &id) const;
    /// Smallest element of that class containing the page-pixel point; empty when none.
    [[nodiscard]] std::string elementAt(QPointF pagePoint, const QString &className) const;
};

} // namespace cc::ui
