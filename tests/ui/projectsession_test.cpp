// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Project life on disk (states.md section 5): create, edit, autosave to its own file, save (autosave gone),
// reopen with the cursor back where it was, recovery offer when the autosave is newer, the recent list.
#include "core/edit/commands.h"
#include "core/media/audiofile.h"
#include "core/model/scorebuilder.h"
#include "core/project/project.h"
#include "core/project/projectfile.h"
#include "generated/settingkeys.h"
#include "ui/copy/projectsession.h"
#include "ui/score/scoreeditor.h"
#include "ui/shell/settings.h"

#include <catch2/catch_test_macros.hpp>

#include <QSignalSpy>
#include <QTest>

#include <filesystem>
#include <fstream>

using namespace cc;
using namespace cc::ui;

namespace
{

struct Fixture {
    std::filesystem::path root = std::filesystem::temp_directory_path() / "copycoach-tests" / "projectsession";
    Settings settings{root / "user"};
    ScoreEditor editor;
    ProjectSession session{editor, settings};

    Fixture()
    {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "user");
        settings.setValue(setting::Key::FilesAutosaveIntervalS, nlohmann::json(1));
    }
};

/// A short stereo WAV of silence, enough to be a source.
std::filesystem::path writeWav(const std::filesystem::path &path)
{
    media::AudioFileWriter writer;
    REQUIRE(writer.open(path, 48000.0, 2, media::AudioFileFormat::WavFloat));
    const std::vector<float> frames(static_cast<std::size_t>(48000 * 2), 0.0F);
    REQUIRE(writer.write(frames));
    return path;
}

model::Score quarters()
{
    model::IdGenerator ids = model::IdGenerator::seeded(31);
    model::ScoreSpec spec;
    spec.measures.assign(2, model::MeasureSpec{{{model::Dur::Quarter, {{1, 3}}}, {model::Dur::Quarter, {{2, 5}}}}});
    return model::buildScore(spec, ids);
}

} // namespace

TEST_CASE("projectsession: create writes project.json, edits make it modified, save clears that and the autosave")
{
    Fixture f;
    const std::filesystem::path folder = f.root / "song";
    REQUIRE_FALSE(f.session.hasProject());
    REQUIRE(f.session.create(folder));
    REQUIRE(std::filesystem::exists(folder / "project.json"));
    REQUIRE(f.session.hasProject());
    REQUIRE(f.session.title() == QStringLiteral("song"));
    REQUIRE_FALSE(f.session.modified());
    REQUIRE(f.session.recent() == std::vector<std::filesystem::path>{folder});

    const QSignalSpy modified(&f.session, &ProjectSession::modifiedChanged);
    f.session.importScore(quarters());
    REQUIRE(f.session.modified());
    REQUIRE(f.session.editCount() == 1);
    REQUIRE(modified.count() == 1);
    f.editor.typeDigit(7);
    REQUIRE(f.session.editCount() == 2);

    f.session.autosaveNow();
    REQUIRE(std::filesystem::exists(folder / "project.autosave.json"));
    REQUIRE(f.session.lastAutosave());
    REQUIRE(f.session.save());
    REQUIRE_FALSE(f.session.modified());
    REQUIRE_FALSE(std::filesystem::exists(folder / "project.autosave.json"));
    REQUIRE(std::filesystem::exists(folder / "project.json"));

    f.editor.undo(); // undoing past the save point is a change too
    REQUIRE(f.session.modified());
    f.editor.redo();
    REQUIRE_FALSE(f.session.modified());
}

TEST_CASE("projectsession: the timer autosaves only new changes, and reopening restores the cursor")
{
    Fixture f;
    const std::filesystem::path folder = f.root / "timed";
    REQUIRE(f.session.create(folder));
    f.session.importScore(quarters());
    REQUIRE(QTest::qWaitFor(
        [&] {
            return std::filesystem::exists(folder / "project.autosave.json");
        },
        3000));
    const auto first = std::filesystem::last_write_time(folder / "project.autosave.json");
    QTest::qWait(1200); // another tick with nothing new: the file is left alone
    REQUIRE(std::filesystem::last_write_time(folder / "project.autosave.json") == first);

    f.editor.move(ScoreEditor::Move::LastBar); // measure 2, tick 0
    REQUIRE(f.session.save());
    f.session.close();
    REQUIRE_FALSE(f.session.hasProject());
    REQUIRE(f.session.open(folder));
    REQUIRE(f.editor.score().measures.size() == 2);
    REQUIRE(f.editor.cursor().value_or(BeatLocation{}).measure == 1);
    REQUIRE_FALSE(f.session.recoverableAutosave());
}

TEST_CASE("projectsession: a newer autosave is offered on open and recovering it leaves the project modified")
{
    Fixture f;
    const std::filesystem::path folder = f.root / "crashed";
    REQUIRE(f.session.create(folder));
    f.session.importScore(quarters());
    REQUIRE(f.session.save());
    f.editor.typeDigit(9); // changed after the save ...
    f.session.autosaveNow(); // ... and autosaved, then the app "dies"
    REQUIRE(std::filesystem::exists(folder / "project.autosave.json"));
    f.session.close();

    const QSignalSpy offered(&f.session, &ProjectSession::recoveryAvailable);
    REQUIRE(f.session.open(folder));
    REQUIRE(offered.count() == 1);
    REQUIRE(f.session.recoverableAutosave());
    REQUIRE_FALSE(f.session.modified()); // the saved file is what shows until the user decides
    REQUIRE(f.editor.score().measures[0].beats[0].notes[0].fret == 3);
    REQUIRE(f.session.recover());
    REQUIRE(f.session.modified());
    REQUIRE(f.editor.score().measures[0].beats[0].notes[0].fret == 9);
    REQUIRE_FALSE(f.session.recoverableAutosave());
    REQUIRE(f.session.save());
    REQUIRE_FALSE(f.session.modified());
}

TEST_CASE("projectsession: an imported score without a project is an unsaved project until save-as names a folder")
{
    Fixture f;
    f.session.importScore(quarters());
    REQUIRE(f.session.hasProject());
    REQUIRE_FALSE(f.session.folder());
    REQUIRE(f.session.modified());
    REQUIRE_FALSE(f.session.save());
    f.session.autosaveNow(); // nowhere to write: nothing happens
    REQUIRE_FALSE(f.session.lastAutosave());
    const std::filesystem::path folder = f.root / "named";
    REQUIRE(f.session.saveAs(folder));
    REQUIRE(f.session.folder() == folder);
    REQUIRE_FALSE(f.session.modified());
    REQUIRE(std::filesystem::exists(folder / "project.json"));
    REQUIRE(f.session.recent().front() == folder);

    f.settings.setValue(setting::Key::FilesRecentCount, nlohmann::json(2));
    REQUIRE(f.session.create(f.root / "a"));
    REQUIRE(f.session.create(f.root / "b"));
    REQUIRE(f.session.recent().size() == 2);
    REQUIRE(f.session.recent().front() == f.root / "b");
}

TEST_CASE("projectsession: a video version plays from its extracted audio in cache/ and keeps its audio track")
{
    Fixture f;
    const std::filesystem::path folder = f.root / "withvideo";
    const std::filesystem::path video = std::filesystem::path(CC_FIXTURE_DIR) / "video" / "gray-steps.mp4";
    REQUIRE(f.session.create(folder));
    const Result<model::Id> id = f.session.importVideo(video);
    REQUIRE(id);
    REQUIRE(std::filesystem::exists(folder / "media" / "gray-steps.mp4"));
    const project::Source source = f.session.activeSource().value_or(project::Source{});
    CHECK(source.id == *id);
    CHECK(source.kind == project::SourceKind::Video);
    CHECK(source.audioTrack == 0);
    CHECK(f.session.activeVideoPath() == folder / "media" / "gray-steps.mp4");
    CHECK(f.session.activeSourcePath() == folder / "cache" / (*id + ".flac"));
    CHECK(f.session.extractedAudioPath(*id) == folder / "cache" / (*id + ".flac"));
    CHECK(std::filesystem::is_directory(folder / "cache"));

    REQUIRE(f.editor.runCommand(edit::setSourceAudioTrack(f.session.project(), *id, 1)));
    CHECK(f.session.activeSource().value_or(project::Source{}).audioTrack == 1);
    CHECK(edit::setSourceAudioTrack(f.session.project(), *id, 1) == nullptr); // same track: no edit
    CHECK(edit::setSourceAudioTrack(f.session.project(), "nope", 0) == nullptr);
    f.editor.undo();
    CHECK(f.session.activeSource().value_or(project::Source{}).audioTrack == 0);

    REQUIRE(f.session.importAudio(writeWav(f.root / "take2.wav")));
    CHECK_FALSE(f.session.activeVideoPath()); // a recording again
}

TEST_CASE("projectsession: a recording is copied into media/, recorded as a source with its hash, and made active")
{
    Fixture f;
    const std::filesystem::path folder = f.root / "withaudio";
    const std::filesystem::path wav = writeWav(f.root / "take1.wav");
    REQUIRE(f.session.create(folder));
    const Result<model::Id> id = f.session.importAudio(wav);
    REQUIRE(id);
    REQUIRE(std::filesystem::exists(folder / "media" / "take1.wav"));
    REQUIRE(f.session.project().sources.size() == 1);
    REQUIRE(f.session.project().sources[0].path == "media/take1.wav");
    REQUIRE(f.session.project().sources[0].hash.size() == 64);
    REQUIRE(f.session.project().sources[0].label == "take1");
    REQUIRE(f.session.activeSourceId() == *id);
    REQUIRE(f.session.activeSourcePath() == folder / "media" / "take1.wav");
    REQUIRE(f.session.modified()); // the source record is an edit
    REQUIRE(f.session.save());
    REQUIRE_FALSE(f.session.importAudio(f.root / "missing.wav"));

    // without a folder (or with copying off) the original path is kept
    f.settings.setValue(setting::Key::FilesCopyMediaIntoProject, nlohmann::json(false));
    const Result<model::Id> second = f.session.importAudio(wav);
    REQUIRE(second);
    REQUIRE(f.session.project().sources[1].path == wav.string());
    REQUIRE(f.session.activeSourcePath() == wav);
    f.session.close();
    REQUIRE(f.session.importAudio(wav));
    REQUIRE_FALSE(f.session.folder());
    REQUIRE(f.session.activeSourcePath() == wav);
}
