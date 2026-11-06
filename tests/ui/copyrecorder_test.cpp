// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Copy practice events (copy-spec.md section 5, stats.md section 4): project open, one play/stop pair per
// playback with the loop and the repeat count, predictions, imports. The engine's signals are driven by hand.
#include "core/project/project.h"
#include "core/stats/event.h"
#include "ui/audio/audioservice.h"
#include "ui/copy/copyrecorder.h"
#include "ui/copy/projectsession.h"
#include "ui/score/scoreeditor.h"
#include "ui/shell/apppaths.h"
#include "ui/shell/settings.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <vector>

using namespace cc;
using namespace cc::ui;

namespace
{

struct CollectingSink final : stats::IStatsSink {
    std::vector<stats::StatsEvent> events;

    void record(const stats::StatsEvent &event) override
    {
        events.push_back(event);
    }
};

struct Fixture {
    std::filesystem::path userDir = std::filesystem::temp_directory_path() / "copycoach-tests" / "copyrecorder";
    AppPaths paths{std::filesystem::path(CC_DATA_DIR), std::filesystem::path(CC_SOUND_DIR), userDir};
    Settings settings{userDir};
    AudioService audio{paths, settings};
    ScoreEditor editor;
    ProjectSession project{editor, settings};
    CollectingSink sink;
    CopyRecorder recorder{sink, project, audio};

    Fixture()
    {
        std::filesystem::create_directories(userDir);
    }

    void createProject()
    {
        static int n = 0;
        REQUIRE(project.create(userDir / ("p" + std::to_string(++n))));
    }
};

} // namespace

TEST_CASE("copyrecorder: creating a project records copy.project.open with its id")
{
    Fixture f;
    f.createProject();
    REQUIRE(f.sink.events.size() == 1);
    CHECK(f.sink.events[0].type == stats::event::copyProjectOpen);
    CHECK(f.sink.events[0].payload["project_id"] == f.project.project().projectId);
}

TEST_CASE("copyrecorder: a play/stop pair carries the loop, the speed and the wrap count")
{
    Fixture f;
    f.createProject();
    f.sink.events.clear();
    f.recorder.setLoop(std::pair<std::size_t, std::size_t>{2, 5}, std::optional<model::Id>{"r1"});

    f.recorder.onStateChanged(audio::EngineState::Playing); // not copy mode: synth playback, no event
    CHECK(f.sink.events.empty());
    f.recorder.onStateChanged(audio::EngineState::Stopped);
    CHECK(f.sink.events.empty());

    f.audio.setCopyMode(true);
    f.recorder.onStateChanged(audio::EngineState::Playing);
    REQUIRE(f.sink.events.size() == 1);
    const nlohmann::json &play = f.sink.events[0].payload;
    CHECK(f.sink.events[0].type == stats::event::copyLoopPlay);
    CHECK(play["range_id"] == "r1");
    CHECK(play["bars"] == nlohmann::json({3, 6}));
    CHECK(play["speed_pct"] == 100);
    CHECK(play["sources"].is_array());

    f.recorder.onSourceFrame(1000);
    f.recorder.onSourceFrame(2000);
    f.recorder.onSourceFrame(500); // wrapped
    f.recorder.onSourceFrame(1500);
    f.recorder.onSourceFrame(100); // wrapped again
    f.recorder.onStateChanged(audio::EngineState::Playing); // repeated signal: no second play
    REQUIRE(f.sink.events.size() == 1);
    f.recorder.onStateChanged(audio::EngineState::Stopped);
    REQUIRE(f.sink.events.size() == 2);
    const nlohmann::json &stop = f.sink.events[1].payload;
    CHECK(f.sink.events[1].type == stats::event::copyLoopStop);
    CHECK(stop["repeats"] == 2);
    CHECK(stop["duration"].is_number_integer());
    CHECK(stop["range_id"] == "r1"); // the stop repeats the loop so one row answers the dock
    CHECK(stop["bars"] == nlohmann::json({3, 6}));
    CHECK(stop["speed_pct"] == 100);
}

TEST_CASE("copyrecorder: without a loop the bars and range are null, and the project reset clears the loop")
{
    Fixture f;
    f.createProject();
    f.audio.setCopyMode(true);
    f.recorder.setLoop(std::pair<std::size_t, std::size_t>{0, 1}, std::nullopt);
    f.createProject(); // a new project forgets the loop
    f.sink.events.clear();
    f.recorder.onStateChanged(audio::EngineState::Playing);
    REQUIRE(f.sink.events.size() == 1);
    CHECK(f.sink.events[0].payload["bars"].is_null());
    CHECK(f.sink.events[0].payload["range_id"].is_null());
}

TEST_CASE("copyrecorder: predictions and imports are one event each")
{
    Fixture f;
    f.createProject();
    f.sink.events.clear();
    f.recorder.recordPredictShow("beats");
    f.recorder.recordPredictAccept("beats", 12);
    f.recorder.recordScoreImport("musicxml");
    REQUIRE(f.sink.events.size() == 3);
    CHECK(f.sink.events[0].type == stats::event::copyPredictShow);
    CHECK(f.sink.events[0].payload["kind"] == "beats");
    CHECK(f.sink.events[1].type == stats::event::copyPredictAccept);
    CHECK(f.sink.events[1].payload["count"] == 12);
    CHECK(f.sink.events[2].type == stats::event::copyScoreImport);
    CHECK(f.sink.events[2].payload["format"] == "musicxml");
}
