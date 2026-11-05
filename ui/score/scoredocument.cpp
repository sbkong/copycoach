// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/score/scoredocument.h"

#include "ui/score/qtdevicecontext.h"

#include <vrv/toolkit.h>

#include <QPainter>

#include <algorithm>
#include <sstream>

namespace cc::ui
{

namespace
{

constexpr int percent = 100;

} // namespace

ScoreDocument::ScoreDocument(const std::filesystem::path &resourceDir)
    : m_toolkit(std::make_unique<vrv::Toolkit>(false))
{
    m_ready = m_toolkit->SetResourcePath(resourceDir.string());
    applyOptions();
}

ScoreDocument::~ScoreDocument() = default;

bool ScoreDocument::ready() const
{
    return m_ready;
}

void ScoreDocument::setInk(const QColor &color, const QString &serifFamily)
{
    m_ink = color;
    m_serifFamily = serifFamily;
    for (auto &page : m_pages) {
        page.reset();
    }
}

void ScoreDocument::setPageWidth(int px)
{
    const int width = std::max(minPageWidthPx, px);
    if (width == m_pageWidthPx) {
        return;
    }
    m_pageWidthPx = width;
    applyOptions();
    reload();
}

void ScoreDocument::setFontScale(int percentValue)
{
    if (percentValue <= 0 || percentValue == m_fontScale) {
        return;
    }
    m_fontScale = percentValue;
    applyOptions();
    reload();
}

/// Verovio's page width is in its own units at 100 %; `scale` shrinks the drawing, so the page gets wider in
/// units to keep the same pixel width.
std::string ScoreDocument::optionsJson() const
{
    std::ostringstream json;
    json << R"({"scale": )" << m_fontScale << R"(, "pageWidth": )" << (m_pageWidthPx * percent / m_fontScale)
         << R"(, "adjustPageHeight": true, "breaks": "auto", "footer": "none", "header": "none"})";
    return json.str();
}

void ScoreDocument::applyOptions()
{
    m_toolkit->SetOptions(optionsJson());
}

void ScoreDocument::reload()
{
    m_pages.clear();
    if (m_mei.empty()) {
        return;
    }
    if (m_toolkit->LoadData(m_mei)) {
        m_pages.resize(static_cast<std::size_t>(std::max(0, m_toolkit->GetPageCount())));
    }
}

bool ScoreDocument::setScore(const model::Score &score, const score::MeiOptions &options)
{
    m_mei = score::writeMei(score, options);
    reload();
    return !m_pages.empty();
}

void ScoreDocument::clear()
{
    m_mei.clear();
    m_pages.clear();
}

bool ScoreDocument::hasScore() const
{
    return !m_mei.empty();
}

const std::string &ScoreDocument::mei() const
{
    return m_mei;
}

int ScoreDocument::pageCount() const
{
    return static_cast<int>(m_pages.size());
}

/// The page size is only known after StartPage, so a throwaway render learns it before the real one records.
const ScoreDocument::Page &ScoreDocument::page(int index)
{
    auto &slot = m_pages.at(static_cast<std::size_t>(index));
    if (slot) {
        return *slot;
    }
    auto page = std::make_unique<Page>();
    const int pageNumber = index + 1;
    {
        QPicture probe;
        QPainter painter(&probe);
        QtDeviceContext dc(&painter);
        m_toolkit->RenderToDeviceContext(pageNumber, &dc);
        page->sizePx = dc.pageSizePx();
    }
    page->picture.setBoundingRect(QRect(QPoint(0, 0), page->sizePx.toSize()));
    {
        QPainter painter(&page->picture);
        QtDeviceContext dc(&painter);
        dc.setDefaultColor(m_ink);
        if (!m_serifFamily.isEmpty()) {
            dc.setSerifFamily(m_serifFamily);
        }
        m_toolkit->RenderToDeviceContext(pageNumber, &dc);
        page->geometry.elements = dc.elements();
        page->geometry.logicalToDevice = dc.logicalToDevice();
        m_unsupportedArcs = dc.unsupportedArcCount();
    }
    slot = std::move(page);
    return *slot;
}

void ScoreDocument::renderDirect(int pageNumber, QtDeviceContext &dc)
{
    dc.setDefaultColor(m_ink);
    if (!m_serifFamily.isEmpty()) {
        dc.setSerifFamily(m_serifFamily);
    }
    m_toolkit->RenderToDeviceContext(pageNumber, &dc);
}

int ScoreDocument::unsupportedArcCount() const
{
    return m_unsupportedArcs;
}

} // namespace cc::ui
