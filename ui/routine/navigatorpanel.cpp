// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/routine/navigatorpanel.h"

#include "core/theory/weekdayplan.h"
#include "ui/routine/routinesession.h"
#include "ui/theme/theme.h"

#include <QDate>
#include <QLabel>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>

namespace cc::ui
{

namespace
{

constexpr int moduleIdRole = Qt::UserRole + 1;
constexpr int modeIdRole = Qt::UserRole + 2;

QString fromStd(const std::string &text)
{
    return QString::fromStdString(text);
}

QString fromView(std::string_view view)
{
    return QString::fromUtf8(view.data(), static_cast<qsizetype>(view.size()));
}

QLabel *groupLabel(const QString &text, const Theme &theme, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setFont(theme.font(theme::TypeStyle::Caption));
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, theme.color(theme::Token::ContentColorSecondary));
    label->setPalette(palette);
    return label;
}

} // namespace

NavigatorPanel::NavigatorPanel(const Theme &theme, RoutineSession &session, QWidget *parent)
    : QWidget(parent)
    , m_theme(&theme)
    , m_session(&session)
    , m_model(new QStandardItemModel(this))
    , m_tree(new QTreeView(this))
    , m_today(new QLabel(this))
{
    auto *layout = new QVBoxLayout(this);
    const int edge = theme.spacing(theme::Spacing::ContentArea);
    layout->setContentsMargins(edge, edge, edge, edge);
    layout->setSpacing(theme.spacing(theme::Spacing::Control));
    layout->addWidget(groupLabel(tr("모듈"), theme, this));
    m_tree->setModel(m_model);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(false);
    m_tree->setItemsExpandable(false);
    m_tree->setAccessibleName(tr("루틴 모듈"));
    layout->addWidget(m_tree, 1);
    layout->addWidget(groupLabel(tr("오늘 루틴"), theme, this));
    m_today->setWordWrap(true);
    layout->addWidget(m_today);
    fillTree();
    updateToday();
    connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &index) {
        if (!index.isValid()) {
            return;
        }
        const QString module = index.data(moduleIdRole).toString();
        const QString mode = index.data(modeIdRole).toString();
        m_session->setModule(module.toStdString());
        if (!mode.isEmpty()) {
            m_session->setMode(mode.toStdString());
        }
    });
    connect(m_session, &RoutineSession::slotsChanged, this, &NavigatorPanel::syncSelection);
}

void NavigatorPanel::fillTree()
{
    m_model->clear();
    for (const routine::ModuleDefinition &module : m_session->definition().modules) {
        auto *item = new QStandardItem(fromStd(module.id) + QLatin1Char(' ') + fromStd(module.name));
        item->setData(fromStd(module.id), moduleIdRole);
        item->setEditable(false);
        item->setFont(m_theme->font(theme::TypeStyle::BodyStrong));
        item->setEnabled(!module.modes.empty()); // modules on hold stay visible but disabled (KDE 2.2)
        for (const routine::ModeDefinition &mode : module.modes) {
            auto *child = new QStandardItem(QStringLiteral("   ") + fromStd(mode.name));
            child->setData(fromStd(module.id), moduleIdRole);
            child->setData(fromStd(mode.id), modeIdRole);
            child->setEditable(false);
            item->appendRow(child);
        }
        m_model->appendRow(item);
    }
    m_tree->expandAll();
    syncSelection();
}

namespace
{

QStandardItem *modeItemOf(const QStandardItem *moduleItem, const QString &mode)
{
    for (int child = 0; child < moduleItem->rowCount(); ++child) {
        QStandardItem *modeItem = moduleItem->child(child);
        if (modeItem->data(modeIdRole) == mode) {
            return modeItem;
        }
    }
    return nullptr;
}

} // namespace

QStandardItem *NavigatorPanel::findModeItem(const QString &module, const QString &mode) const
{
    for (int row = 0; row < m_model->rowCount(); ++row) {
        const QStandardItem *item = m_model->item(row);
        if (item->data(moduleIdRole) == module) {
            return modeItemOf(item, mode);
        }
    }
    return nullptr;
}

void NavigatorPanel::syncSelection()
{
    const QStandardItem *item = findModeItem(fromStd(m_session->state().moduleId), fromStd(m_session->state().modeId));
    if (item != nullptr) {
        const QSignalBlocker blocker(m_tree->selectionModel());
        m_tree->setCurrentIndex(item->index());
    }
}

void NavigatorPanel::updateToday()
{
    const int qtDay = QDate::currentDate().dayOfWeek(); // 1 = Monday .. 7 = Sunday
    const auto weekday = static_cast<theory::Weekday>(qtDay % 7);
    const theory::WeekdayPlan &plan = m_session->definition().weekdayPlan[static_cast<std::size_t>(weekday)];
    const QString keys =
        fromView(theory::flatName(plan.keys[0])) + QStringLiteral(" · ") + fromView(theory::flatName(plan.keys[1]));
    const QString mode = tr("선법 %1").arg(plan.mode);
    m_today->setText(plan.review ? tr("복습 · %1 (기본값, 직접 고르세요)").arg(keys)
                                 : mode + QStringLiteral(" · ") + keys);
}

} // namespace cc::ui
