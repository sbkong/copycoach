// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Memos and lyrics in the copy inspector (copy-spec.md section 7-6): typing lands in the project as one
// command per pause, a range picked in the list edits its own memo, undo steps back through it, and a reload
// refreshes the boxes without clobbering text being typed.
#include "core/edit/commands.h"
#include "core/model/scorebuilder.h"
#include "ui/copy/copyinspector.h"
#include "ui/score/scoreeditor.h"
#include "ui/shell/apppaths.h"
#include "ui/theme/theme.h"

#include <catch2/catch_test_macros.hpp>

#include <QPlainTextEdit>
#include <QTest>

#include <filesystem>

using namespace cc;
using namespace cc::ui;

namespace
{

struct Fixture {
    AppPaths paths{std::filesystem::path(CC_DATA_DIR),
                   std::filesystem::path(CC_SOUND_DIR),
                   std::filesystem::temp_directory_path() / "copycoach-tests" / "copyinspector"};
    Theme theme;
    ScoreEditor editor;

    Fixture()
    {
        REQUIRE(theme.load(paths.theme(true)));
        model::IdGenerator ids = model::IdGenerator::seeded(51);
        model::ScoreSpec spec;
        spec.measures.assign(4, model::MeasureSpec{{{model::Dur::Whole, {{1, 3}}}}});
        editor.setScore(model::buildScore(spec, ids));
    }
};

QPlainTextEdit *boxNamed(const QWidget &root, const QString &name)
{
    for (QPlainTextEdit *edit : root.findChildren<QPlainTextEdit *>()) {
        if (edit->accessibleName() == name) {
            return edit;
        }
    }
    FAIL("no text box " << name.toStdString());
}

} // namespace

TEST_CASE("copyinspector: memo and lyrics commit after a pause as commands and undo one pause at a time")
{
    Fixture f;
    CopyInspector inspector(f.theme, f.editor);
    QPlainTextEdit *memo = boxNamed(inspector, QStringLiteral("곡 메모"));
    QPlainTextEdit *lyrics = boxNamed(inspector, QStringLiteral("가사"));

    memo->setPlainText(QStringLiteral("count it off slowly"));
    REQUIRE(f.editor.project().memos.project.empty()); // not yet: the pause has not passed
    inspector.commitNow();
    REQUIRE(f.editor.project().memos.project == "count it off slowly");
    REQUIRE(f.editor.undoStack().undoDepth() == 1);

    lyrics->setPlainText(QStringLiteral("la la"));
    REQUIRE(QTest::qWaitFor(
        [&] {
            return f.editor.project().lyrics == "la la";
        },
        CopyInspector::commitDelayMs * 3));
    REQUIRE(f.editor.undoStack().undoDepth() == 2);

    f.editor.undo();
    REQUIRE(f.editor.project().lyrics.empty());
    REQUIRE(lyrics->toPlainText().isEmpty()); // the box follows the project
    REQUIRE(memo->toPlainText() == QStringLiteral("count it off slowly"));
}

TEST_CASE("copyinspector: ranges are listed, the chosen one takes its own memo, removal clears it")
{
    Fixture f;
    CopyInspector inspector(f.theme, f.editor);
    model::IdGenerator ids = model::IdGenerator::seeded(52);
    project::Range a{ids.next(), "ii-V", 0, 1, 0, 80, 0};
    project::Range b{ids.next(), "ending", 2, 3, 0, 100, 0};
    REQUIRE(f.editor.runCommand(edit::setRange(f.editor.project(), a)));
    REQUIRE(f.editor.runCommand(edit::setRange(f.editor.project(), b)));
    REQUIRE_FALSE(inspector.selectedRange());
    QPlainTextEdit *rangeMemo = boxNamed(inspector, QStringLiteral("구간 메모"));
    REQUIRE_FALSE(rangeMemo->isEnabled());

    int chosen = 0;
    QObject::connect(&inspector, &CopyInspector::rangeChosen, &inspector, [&chosen] {
        ++chosen;
    });
    inspector.selectRange(b.id);
    REQUIRE(inspector.selectedRange() == b.id);
    REQUIRE(rangeMemo->isEnabled());
    rangeMemo->setPlainText(QStringLiteral("watch the b9"));
    inspector.commitNow();
    REQUIRE(f.editor.project().memos.ranges.at(b.id) == "watch the b9");

    inspector.selectRange(a.id);
    REQUIRE(rangeMemo->toPlainText().isEmpty());
    inspector.selectRange(b.id);
    REQUIRE(rangeMemo->toPlainText() == QStringLiteral("watch the b9"));
    REQUIRE(chosen == 0); // programmatic selection is not the user's pick

    REQUIRE(f.editor.runCommand(edit::removeRange(f.editor.project(), b.id)));
    REQUIRE_FALSE(inspector.selectedRange());
    REQUIRE(f.editor.project().memos.ranges.empty());
    inspector.setSelectedMeasures(std::pair<std::size_t, std::size_t>{1, 2});
    inspector.setSelectedMeasures(std::nullopt);
}
