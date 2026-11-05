// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// The Verovio build (cmake/Verovio.cmake): resources load from <build>/data/verovio and a MusicXML file with
// staff + tab goes through to MEI and pages. The importer's own checks live with the importer (D-4 golden files).
#include <catch2/catch_test_macros.hpp>

#include <vrv/toolkit.h>

#include <string>

TEST_CASE("verovio: loads its resources and turns the tab fixture into MEI and at least one page")
{
    vrv::Toolkit toolkit(false);
    REQUIRE(toolkit.SetResourcePath(CC_VEROVIO_DATA_DIR));
    REQUIRE(toolkit.LoadFile(std::string(CC_FIXTURE_DIR) + "/score/tab-techniques.musicxml"));
    REQUIRE(toolkit.GetPageCount() >= 1);
    const std::string mei = toolkit.GetMEI();
    REQUIRE(mei.contains("<mei"));
    REQUIRE(mei.contains("<tabGrp")); // the tab staff survived the import
}
