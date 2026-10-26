// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/theory/constants.h"
#include "core/version.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("version: appVersion is a dev version string")
{
    REQUIRE(cc::appVersion() == "0.1.0-dev");
}

TEST_CASE("constants: music conventions match coding-standards section 17")
{
    REQUIRE(cc::theory::ticksPerQuarter == 960);
    REQUIRE(cc::theory::middleC == 60);
    REQUIRE(cc::theory::a4 == 69);
    REQUIRE(cc::theory::firstString == 1);
    REQUIRE(cc::theory::tempoMinBpm < cc::theory::tempoMaxBpm);
}
