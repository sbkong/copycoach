// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Every registry action becomes a QAction with its default shortcut; templates expand; contexts gate enabling.
#include "ui/shell/actionregistry.h"

#include <catch2/catch_test_macros.hpp>

#include <QKeySequence>

using namespace cc;
using namespace cc::ui;

TEST_CASE("action registry: one QAction per id, templates expanded over their range")
{
    const ActionRegistry registry;
    std::size_t expected = 0;
    for (const action::Info &info : action::infos) {
        expected += info.rangeMax > 0 ? static_cast<std::size_t>(info.rangeMax - info.rangeMin + 1) : 1;
    }
    REQUIRE(registry.all().size() == expected);
    QAction *play = registry.action(action::Id::TransportPlayPause);
    REQUIRE(play != nullptr);
    REQUIRE(play->shortcut() == QKeySequence(Qt::Key_Space));
    REQUIRE(play->objectName() == QStringLiteral("transport.play-pause"));
    REQUIRE(registry.action(action::Id::RoutineChordSelectN) == nullptr);
    QAction *select3 = registry.action(action::Id::RoutineChordSelectN, 3);
    REQUIRE(select3 != nullptr);
    REQUIRE(select3->shortcut() == QKeySequence(Qt::Key_3));
    REQUIRE(select3->objectName() == QStringLiteral("routine.chord.select-3"));
    REQUIRE(registry.action(action::Id::RoutineChordSelectN, 9) == nullptr);
    REQUIRE(registry.action(action::Id::RoutineTodayReassign)->shortcuts().isEmpty());
}

TEST_CASE("action registry: the active context enables its own and global actions only")
{
    ActionRegistry registry;
    registry.setActiveContext(workspace::Context::Routine);
    REQUIRE(registry.action(action::Id::RoutineChordNext)->isEnabled());
    REQUIRE(registry.action(action::Id::TransportLoop)->isEnabled());
    REQUIRE_FALSE(registry.action(action::Id::CopySpeedDown)->isEnabled());
    registry.setActiveContext(workspace::Context::Copy);
    REQUIRE_FALSE(registry.action(action::Id::RoutineChordNext)->isEnabled());
    REQUIRE(registry.action(action::Id::CopySpeedDown)->isEnabled());
    REQUIRE(ActionRegistry::contextOf(action::Id::EditorRest) == workspace::Context::Editor);
}

TEST_CASE("action registry: the focus context adds the focused widget's actions and the workspace switch drops it")
{
    ActionRegistry registry;
    registry.setActiveContext(workspace::Context::Copy);
    REQUIRE_FALSE(registry.action(action::Id::EditorRest)->isEnabled());
    REQUIRE_FALSE(registry.action(action::Id::EditorInputDigitN, 3)->isEnabled());
    registry.setFocusContext(workspace::Context::Editor);
    REQUIRE(registry.action(action::Id::EditorRest)->isEnabled());
    REQUIRE(registry.action(action::Id::EditorInputDigitN, 3)->isEnabled());
    REQUIRE(registry.action(action::Id::CopySpeedDown)->isEnabled()); // the workspace stays
    REQUIRE_FALSE(registry.action(action::Id::RoutineChordNext)->isEnabled());
    REQUIRE(registry.focusContext() == workspace::Context::Editor);
    registry.setFocusContext(workspace::Context::Global);
    REQUIRE_FALSE(registry.action(action::Id::EditorRest)->isEnabled());
    registry.setFocusContext(workspace::Context::Editor);
    registry.setActiveContext(workspace::Context::Routine);
    REQUIRE(registry.focusContext() == workspace::Context::Global);
    REQUIRE_FALSE(registry.action(action::Id::EditorRest)->isEnabled());
}
