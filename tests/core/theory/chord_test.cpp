// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/theory/chord.h"

#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <string>
#include <vector>

using namespace cc::theory;

namespace
{

std::string nameOf(std::initializer_list<PitchClass> pitchClasses)
{
    const std::vector<PitchClass> set(pitchClasses);
    return nameChordOf(set);
}

} // namespace

TEST_CASE("nameChordOf names a pitch-class set from whichever note is its root (the upper four of section 2.7)")
{
    REQUIRE(nameOf({4, 7, 11, 2}) == "Em7"); // Cmaj9 above C
    REQUIRE(nameOf({7, 0, 9, 4}) == "Am7"); // Fmaj9 above F, any order
    REQUIRE(nameOf({7, 11, 2}) == "G"); // Em7 with R + 5 on top: a triad
    REQUIRE(nameOf({2, 5, 9}) == "Dm"); // Bm7b5 with b5 + R on top
    REQUIRE(nameOf({9, 0, 3, 7}) == "Am7b5"); // B7(b9 b13) above B
    REQUIRE(nameOf({11, 5, 4, 9}).empty()); // G13: 3 7 13 9 spell no tertian chord
    REQUIRE(nameOf({4, 11}).empty()); // a shell alone
}
