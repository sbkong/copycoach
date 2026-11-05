// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Model files on disk and the download job (states.md section 2): a local file:// release stands in for GitHub,
// the job lands the file by name once the size and SHA-256 match, and a wrong hash leaves nothing behind.
#include "ui/copy/modelfiles.h"

#include <catch2/catch_test_macros.hpp>

#include <QUrl>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace cc;
using namespace cc::ui;

namespace
{

const std::filesystem::path s_root = std::filesystem::temp_directory_path() / "copycoach-tests" / "modelfiles";

/// "hello\n" published as a release file; its SHA-256 is the well-known one.
constexpr predict::ModelFile helloFile{"hello.bin",
                                       6,
                                       "5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03"};

std::filesystem::path writeRelease()
{
    const std::filesystem::path release = s_root / "release";
    std::filesystem::create_directories(release);
    std::ofstream(release / "hello.bin", std::ios::binary) << "hello\n";
    return release;
}

Result<void> runNow(const jobs::Work &work)
{
    const std::stop_source never;
    return work(never.get_token(), [](double) { });
}

} // namespace

TEST_CASE("modelfiles: the download job lands a file whose size and hash match the manifest")
{
    const std::filesystem::path release = writeRelease();
    const std::filesystem::path dir = s_root / "models";
    std::filesystem::remove_all(dir);
    REQUIRE_FALSE(modelPresent(dir, helloFile));
    REQUIRE(missingModels(dir, std::vector{helloFile}).size() == 1);

    const QUrl base =
        QUrl::fromLocalFile(QString::fromStdWString((release / "x").wstring())); // "x" is replaced by the name
    REQUIRE(runNow(downloadModels(base, dir, {helloFile})));
    CHECK(modelPresent(dir, helloFile));
    CHECK(missingModels(dir, std::vector{helloFile}).empty());
    CHECK_FALSE(std::filesystem::exists(modelPath(dir, helloFile).string() + ".part"));
}

TEST_CASE("modelfiles: a hash mismatch is ModelCorrupt and the partial file is removed")
{
    const std::filesystem::path release = writeRelease();
    const std::filesystem::path dir = s_root / "models-bad";
    std::filesystem::remove_all(dir);
    constexpr predict::ModelFile wrong{"hello.bin",
                                       6,
                                       "0000000000000000000000000000000000000000000000000000000000000000"};
    const QUrl base = QUrl::fromLocalFile(QString::fromStdWString((release / "x").wstring()));
    const Result<void> r = runNow(downloadModels(base, dir, {wrong}));
    REQUIRE_FALSE(r);
    CHECK(r.error().code == ErrorCode::ModelCorrupt);
    CHECK_FALSE(std::filesystem::exists(modelPath(dir, wrong)));
    CHECK_FALSE(std::filesystem::exists(modelPath(dir, wrong).string() + ".part"));
}

TEST_CASE("modelfiles: a missing release file is DownloadFailed")
{
    const std::filesystem::path dir = s_root / "models-missing";
    constexpr predict::ModelFile absent{"nope.bin", 1, "00"};
    const QUrl base = QUrl::fromLocalFile(QString::fromStdWString((s_root / "void" / "x").wstring()));
    const Result<void> r = runNow(downloadModels(base, dir, {absent}));
    REQUIRE_FALSE(r);
    CHECK(r.error().code == ErrorCode::DownloadFailed);
}

TEST_CASE("modelfiles: the beat this paths live under the user's models folder")
{
    const AppPaths paths{{}, {}, s_root / "user"};
    const predict::BeatThisFiles files = beatThisFiles(beatThisDir(paths), predict::BeatThisVariant::Small);
    CHECK(files.model.filename() == "beat_this-small0.onnx");
    CHECK(files.filterbank.filename() == "mel-filterbank.bin");
    CHECK(files.model.parent_path() == s_root / "user" / "models" / "beat_this");
}
