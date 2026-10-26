// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Freezes the 12-key shell loop root table (routine-spec.md section 2.2 rule 4: "fixed as a test at implementation").
// Run cc_tests with CC_DUMP_SHELL_TABLE=1 to print the table as JSON when the rule changes on purpose.
#include "core/theory/shellvoicing.h"
#include "fixtures.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

using namespace cc::theory;

namespace
{

nlohmann::json rootsOf(PitchClass key, LoopOrder order)
{
    nlohmann::json roots = nlohmann::json::array();
    for (const ShellSlot &slot : shellLoop(key, order, Tuning::standardSix())) {
        roots.push_back({slot.root.string, slot.root.fret});
    }
    return roots;
}

} // namespace

TEST_CASE("shell: 12-key root table matches the frozen fixture (shell-loop-12keys.json)")
{
    nlohmann::json table;
    for (int key = 0; key < 12; ++key) {
        table[std::string(flatName(key))] = {{"circle5", rootsOf(key, LoopOrder::Circle5)},
                                             {"scale", rootsOf(key, LoopOrder::Scale)}};
    }
    if (std::getenv("CC_DUMP_SHELL_TABLE") != nullptr) {
        std::cout << table.dump(1) << '\n';
    }
    const auto frozen = cc::test::loadRoutineFixture("shell-loop-12keys");
    for (int key = 0; key < 12; ++key) {
        const std::string name(flatName(key));
        INFO("key " << name);
        REQUIRE(table[name] == frozen[name]);
    }
}
