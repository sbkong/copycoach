// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/shell/commandpalette.h"

#include "generated/settingkeys.h"
#include "ui/shell/actionregistry.h"
#include "ui/shell/settings.h"
#include "ui/theme/theme.h"

#include <QAction>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QRegularExpression>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <map>

namespace cc::ui
{

namespace
{

using theme::Token;

constexpr int paletteWidth = 640;
constexpr int paletteHeight = 440;
constexpr int rowHeight = 28;
constexpr int tagWidth = 56;
constexpr int keyWidth = 120;

/// Context tag per id prefix (mockup M9 shows the menu group for global actions, the workspace otherwise).
QString contextTag(const QAction *action)
{
    static const std::map<QString, QString> byPrefix{
        {QStringLiteral("file"), QStringLiteral("파일")},
        {QStringLiteral("edit"), QStringLiteral("편집")},
        {QStringLiteral("view"), QStringLiteral("보기")},
        {QStringLiteral("workspace"), QStringLiteral("보기")},
        {QStringLiteral("instrument"), QStringLiteral("보기")},
        {QStringLiteral("fretboard"), QStringLiteral("보기")},
        {QStringLiteral("transport"), QStringLiteral("재생")},
        {QStringLiteral("routine"), QStringLiteral("루틴")},
        {QStringLiteral("copy"), QStringLiteral("카피")},
        {QStringLiteral("app"), QStringLiteral("설정")},
        {QStringLiteral("help"), QStringLiteral("도움말")},
        {QStringLiteral("editor"), QStringLiteral("편집기")},
        {QStringLiteral("video"), QStringLiteral("영상")},
        {QStringLiteral("fingering"), QStringLiteral("카피")},
    };
    const QString prefix = action->objectName().section(QLatin1Char('.'), 0, 0);
    const auto it = byPrefix.find(prefix);
    return it == byPrefix.end() ? prefix : it->second;
}

/// Why a listed action cannot run now: the tooltip set by the shell, else the context it belongs to.
QString disabledReason(const QAction *action)
{
    if (!action->toolTip().isEmpty() && action->toolTip() != action->text()) {
        return action->toolTip();
    }
    return QObject::tr("%1 작업공간에서").arg(contextTag(action));
}

class PaletteDelegate final : public QStyledItemDelegate
{
public:
    PaletteDelegate(const Theme &theme, QObject *parent)
        : QStyledItemDelegate(parent)
        , m_theme(&theme)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const bool enabled = index.data(PaletteModel::enabledRole).toBool();
        const bool selected = (option.state & QStyle::State_Selected) != 0;
        painter->save();
        if (selected) {
            painter->fillRect(option.rect, m_theme->color(Token::AccentBackgroundColorDefault));
        }
        const int pad = m_theme->spacing(theme::Spacing::Control);
        QRect tag = option.rect.adjusted(pad, 0, 0, 0);
        tag.setWidth(tagWidth);
        painter->setFont(m_theme->font(theme::TypeStyle::Caption));
        painter->setPen(m_theme->color(Token::ContentColorSecondary));
        painter->drawText(tag, Qt::AlignVCenter | Qt::AlignLeft, index.data(PaletteModel::contextRole).toString());
        const QRect text = option.rect.adjusted(pad + tagWidth, 0, -keyWidth, 0);
        painter->setFont(m_theme->font(theme::TypeStyle::Body));
        painter->setPen(m_theme->color(enabled ? Token::ContentColorDefault : Token::ContentColorDisabled));
        QString label = index.data(Qt::DisplayRole).toString();
        if (!enabled) {
            label += QStringLiteral(" · ") + index.data(PaletteModel::reasonRole).toString();
        }
        painter->drawText(text,
                          Qt::AlignVCenter | Qt::AlignLeft,
                          option.fontMetrics.elidedText(label, Qt::ElideRight, text.width()));
        QRect key = option.rect.adjusted(0, 0, -pad, 0);
        key.setLeft(key.right() - keyWidth);
        painter->setFont(m_theme->font(theme::TypeStyle::Caption, true));
        painter->setPen(m_theme->color(Token::ContentColorSecondary));
        painter->drawText(key, Qt::AlignVCenter | Qt::AlignRight, index.data(PaletteModel::keyRole).toString());
        painter->restore();
    }

    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        return {QStyledItemDelegate::sizeHint(option, index).width(), rowHeight};
    }

private:
    const Theme *m_theme;
};

} // namespace

// ---- model ----------------------------------------------------------------------------------------------------------

PaletteModel::PaletteModel(const ActionRegistry &actions, QObject *parent)
    : QAbstractListModel(parent)
    , m_actions(&actions)
    , m_goTo(new QAction(this))
{
    rebuild();
}

int PaletteModel::matchScore(const QString &query, const QString &text)
{
    if (query.isEmpty()) {
        return 1;
    }
    const QString haystack = text.toLower();
    const QString needle = query.toLower();
    if (haystack.startsWith(needle)) {
        return 3;
    }
    if (haystack.contains(needle)) {
        return 2;
    }
    qsizetype position = 0;
    for (const QChar c : needle) {
        position = haystack.indexOf(c, position);
        if (position < 0) {
            return 0;
        }
        ++position;
    }
    return 1;
}

void PaletteModel::setQuery(const QString &query)
{
    m_query = query.trimmed();
    rebuild();
}

void PaletteModel::setRecent(const QStringList &actionIds)
{
    m_recent = actionIds;
    rebuild();
}

/// Matches on label and id; order = recent (most recent first), then match strength, then label.
std::optional<int> PaletteModel::measureNumberOf(const QString &query)
{
    static const QRegularExpression pattern(QStringLiteral("^\\s*(?:마디\\s*)?(\\d{1,4})\\s*$"));
    const QRegularExpressionMatch match = pattern.match(query);
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    return match.captured(1).toInt();
}

QAction *PaletteModel::goToAction() const
{
    return m_goTo;
}

void PaletteModel::rebuild()
{
    beginResetModel();
    m_rows.clear();
    if (const auto measure = measureNumberOf(m_query)) {
        m_goTo->setText(tr("마디 %1으로 이동").arg(*measure));
        m_goTo->setData(*measure);
        m_rows.push_back(m_goTo);
    }
    std::vector<std::pair<int, QAction *>> scored;
    for (QAction *action : m_actions->all()) {
        const int score = std::max(matchScore(m_query, action->text()), matchScore(m_query, action->objectName()));
        if (score > 0) {
            scored.emplace_back(score, action);
        }
    }
    const auto recentRank = [this](const QAction *action) {
        const qsizetype index = m_recent.indexOf(action->objectName());
        return index < 0 ? m_recent.size() : index;
    };
    std::ranges::stable_sort(scored, [&recentRank](const auto &a, const auto &b) {
        const qsizetype ra = recentRank(a.second);
        const qsizetype rb = recentRank(b.second);
        if (ra != rb) {
            return ra < rb;
        }
        if (a.first != b.first) {
            return a.first > b.first;
        }
        return a.second->text() < b.second->text();
    });
    for (const auto &[score, action] : scored) {
        m_rows.push_back(action);
    }
    endResetModel();
}

int PaletteModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QAction *PaletteModel::actionAt(int row) const
{
    return row >= 0 && std::cmp_less(row, m_rows.size()) ? m_rows[static_cast<std::size_t>(row)] : nullptr;
}

QVariant PaletteModel::data(const QModelIndex &index, int role) const
{
    const QAction *action = actionAt(index.row());
    if (action == nullptr) {
        return {};
    }
    switch (role) {
    case Qt::DisplayRole:
        return action->text().remove(QLatin1Char('&'));
    case contextRole:
        return contextTag(action);
    case keyRole:
        return action->shortcut().toString(QKeySequence::NativeText);
    case enabledRole:
        return action->isEnabled();
    case reasonRole:
        return disabledReason(action);
    default:
        return {};
    }
}

// ---- popup
// ------------------------------------------------------------------------------------------------------------

CommandPalette::CommandPalette(const Theme &theme, const ActionRegistry &actions, Settings &settings, QWidget *parent)
    : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint)
    , m_theme(&theme)
    , m_settings(&settings)
    , m_model(new PaletteModel(actions, this))
    , m_input(new QLineEdit(this))
    , m_list(new QListView(this))
    , m_scope(new QLabel(this))
{
    resize(paletteWidth, paletteHeight);
    auto *layout = new QVBoxLayout(this);
    const int edge = theme.spacing(theme::Spacing::ContentArea);
    layout->setContentsMargins(edge, edge, edge, edge);
    layout->setSpacing(theme.spacing(theme::Spacing::Control));
    m_input->setPlaceholderText(tr("동작 검색…"));
    m_input->setAccessibleName(tr("커맨드 팔레트 검색"));
    m_input->installEventFilter(this);
    layout->addWidget(m_input);
    m_list->setModel(m_model);
    m_list->setItemDelegate(new PaletteDelegate(theme, m_list));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setAccessibleName(tr("커맨드 팔레트 결과"));
    layout->addWidget(m_list, 1);
    auto *footer = new QLabel(tr("↑↓ 이동 · Enter 실행 · Esc 닫기 · 최근 실행이 위에"), this);
    footer->setFont(theme.font(theme::TypeStyle::Caption));
    layout->addWidget(footer);
    m_scope->setFont(theme.font(theme::TypeStyle::Caption));
    connect(m_input, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_model->setQuery(text);
        m_list->setCurrentIndex(m_model->index(0, 0));
    });
    connect(m_list, &QListView::activated, this, [this](const QModelIndex &) {
        runCurrent();
    });
}

PaletteModel *CommandPalette::model() const
{
    return m_model;
}

QLineEdit *CommandPalette::input() const
{
    return m_input;
}

QListView *CommandPalette::list() const
{
    return m_list;
}

void CommandPalette::open()
{
    const nlohmann::json recent = m_settings->value(setting::Key::AppPaletteRecent);
    QStringList ids;
    for (const auto &item : recent) {
        if (item.is_string()) {
            ids.append(QString::fromStdString(item.get<std::string>()));
        }
    }
    m_model->setRecent(ids);
    m_input->clear();
    m_model->setQuery(QString());
    m_list->setCurrentIndex(m_model->index(0, 0));
    if (const QWidget *host = parentWidget()) {
        const QPoint top = host->mapToGlobal(QPoint((host->width() - width()) / 2, host->height() / 8));
        move(top);
    }
    show();
    m_input->setFocus();
}

void CommandPalette::moveSelection(int delta)
{
    const int count = m_model->rowCount();
    if (count == 0) {
        return;
    }
    const int row = std::clamp(m_list->currentIndex().row() + delta, 0, count - 1);
    m_list->setCurrentIndex(m_model->index(row, 0));
}

void CommandPalette::rememberRecent(const QString &actionId)
{
    nlohmann::json recent = nlohmann::json::array();
    recent.push_back(actionId.toStdString());
    for (const auto &item : m_settings->value(setting::Key::AppPaletteRecent)) {
        if (item.is_string() && item.get<std::string>() != actionId.toStdString() && recent.size() < maxRecent) {
            recent.push_back(item);
        }
    }
    m_settings->setValue(setting::Key::AppPaletteRecent, recent);
}

void CommandPalette::runCurrent()
{
    QAction *action = m_model->actionAt(m_list->currentIndex().row());
    if (action == nullptr || !action->isEnabled()) {
        return;
    }
    if (action == m_model->goToAction()) {
        hide();
        emit goToMeasureRequested(action->data().toInt());
        return;
    }
    rememberRecent(action->objectName());
    hide();
    action->trigger();
}

void CommandPalette::openWithQuery(const QString &query)
{
    open();
    m_input->setText(query);
}

bool CommandPalette::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast) QEvent::type() was checked
        const auto *key = static_cast<const QKeyEvent *>(event);
        if (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up) {
            moveSelection(key->key() == Qt::Key_Down ? 1 : -1);
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            runCurrent();
            return true;
        }
        if (key->key() == Qt::Key_Escape) {
            hide();
            return true;
        }
    }
    return QFrame::eventFilter(watched, event);
}

void CommandPalette::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), m_theme->color(Token::BackgroundLayer2Color)); // mockup M9: layer-2 + emphasis border
    painter.setPen(m_theme->color(Token::BorderColorEmphasis));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

} // namespace cc::ui
