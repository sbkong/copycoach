// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Command palette (components.md section 2.1, M9): fuzzy matching, recent-first order, disabled reasons, run.
#include "ui/shell/actionregistry.h"
#include "ui/shell/commandpalette.h"
#include "ui/shell/settings.h"
#include "ui/theme/theme.h"

#include <catch2/catch_test_macros.hpp>

#include <QLineEdit>
#include <QListView>
#include <QSignalSpy>
#include <QTest>
#include <QWidget>
#include <filesystem>

using namespace cc;
using namespace cc::ui;

namespace
{

struct Fixture {
    std::filesystem::path userDir = std::filesystem::temp_directory_path() / "copycoach-tests" / "palette";
    Settings settings{userDir};
    Theme theme;
    ActionRegistry actions;
    QWidget host;

    Fixture()
    {
        std::filesystem::create_directories(userDir);
        REQUIRE(theme.load(std::filesystem::path(CC_DATA_DIR) / "ui" / "theme-dark.json"));
        actions.setActiveContext(workspace::Context::Routine);
        host.resize(1280, 720);
        host.show();
    }
};

} // namespace

TEST_CASE("palette: match scores prefix > substring > letters in order > none")
{
    REQUIRE(PaletteModel::matchScore(QStringLiteral("코드"), QStringLiteral("코드 3 바로 가기")) == 3);
    REQUIRE(PaletteModel::matchScore(QStringLiteral("코드"), QStringLiteral("다음 코드")) == 2);
    REQUIRE(PaletteModel::matchScore(QStringLiteral("tpp"), QStringLiteral("transport.play-pause")) == 1);
    REQUIRE(PaletteModel::matchScore(QStringLiteral("xyz"), QStringLiteral("transport.play-pause")) == 0);
    REQUIRE(PaletteModel::matchScore(QString(), QStringLiteral("anything")) == 1);
}

TEST_CASE("palette: every action is listed, the query narrows, recent ones come first, disabled rows explain")
{
    Fixture f;
    CommandPalette palette(f.theme, f.actions, f.settings, &f.host);
    palette.open();
    QTest::qWait(30);
    const PaletteModel *model = palette.model();
    REQUIRE(model->rowCount() == static_cast<int>(f.actions.all().size()));

    palette.input()->setText(QStringLiteral("코드"));
    REQUIRE(model->rowCount() > 0);
    REQUIRE(model->rowCount() < static_cast<int>(f.actions.all().size()));
    REQUIRE(model->data(model->index(0, 0), Qt::DisplayRole).toString().contains(QStringLiteral("코드")));
    palette.input()->setText(QStringLiteral("선택 구간 루프"));
    REQUIRE(model->rowCount() >= 1);
    REQUIRE_FALSE(model->data(model->index(0, 0), PaletteModel::enabledRole).toBool()); // copy context in routine
    REQUIRE(model->data(model->index(0, 0), PaletteModel::reasonRole).toString()
            == QStringLiteral("카피 작업공간에서"));
    REQUIRE(model->data(model->index(0, 0), PaletteModel::contextRole).toString() == QStringLiteral("카피"));

    f.settings.setValue(setting::Key::AppPaletteRecent, nlohmann::json::array({"transport.loop", "file.new"}));
    palette.open();
    REQUIRE(model->actionAt(0)->objectName() == QStringLiteral("transport.loop"));
    REQUIRE(model->actionAt(1)->objectName() == QStringLiteral("file.new"));
}

TEST_CASE("palette: Enter runs the selected enabled action and records it as recent; Esc closes")
{
    Fixture f;
    CommandPalette palette(f.theme, f.actions, f.settings, &f.host);
    const QSignalSpy triggered(f.actions.action(action::Id::TransportLoop), &QAction::triggered);
    palette.open();
    palette.input()->setText(QStringLiteral("루프 켬"));
    QTest::keyClick(palette.input(), Qt::Key_Return);
    REQUIRE(triggered.count() == 1);
    REQUIRE_FALSE(palette.isVisible());
    const nlohmann::json recent = f.settings.value(setting::Key::AppPaletteRecent);
    REQUIRE(recent.size() == 1);
    REQUIRE(recent[0] == "transport.loop");

    palette.open();
    REQUIRE(palette.isVisible());
    QTest::keyClick(palette.input(), Qt::Key_Escape);
    REQUIRE_FALSE(palette.isVisible());

    palette.open();
    palette.input()->setText(QStringLiteral("선택 구간 루프")); // disabled: Enter does nothing, stays open
    QTest::keyClick(palette.input(), Qt::Key_Return);
    REQUIRE(palette.isVisible());
}
