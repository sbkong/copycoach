// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// JobService and the status bar chip (states.md section 2): events reach the main thread, the chip shows
// "ratio + time left" or busy + elapsed, cancel answers at once with "취소 중…", and a finished job leaves the
// list with one finished signal.
#include "ui/jobs/jobservice.h"
#include "ui/shell/apppaths.h"
#include "ui/shell/jobschip.h"
#include "ui/theme/theme.h"

#include <catch2/catch_test_macros.hpp>

#include <QTest>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <thread>

using namespace cc;
using namespace cc::ui;
using namespace std::chrono_literals;

namespace
{

struct Fixture {
    AppPaths paths{std::filesystem::path(CC_DATA_DIR),
                   std::filesystem::path(CC_SOUND_DIR),
                   std::filesystem::temp_directory_path() / "copycoach-tests" / "jobservice"};
    Theme theme;
    JobService jobs;
    int finishedCount = 0;
    std::optional<Error> lastError;

    Fixture()
    {
        REQUIRE(theme.load(paths.theme(true)));
        QObject::connect(&jobs, &JobService::finished, &jobs, [this](const jobs::JobEvent &event) {
            ++finishedCount;
            lastError = event.error;
        });
    }
};

/// Holds at `fraction` until released (or stopped).
jobs::Work holdAt(double fraction, std::atomic<bool> &release)
{
    return [fraction, &release](const std::stop_token &stop, const jobs::Report &report) -> Result<void> {
        report(fraction);
        while (!release && !stop.stop_requested()) {
            std::this_thread::sleep_for(1ms);
        }
        return stop.stop_requested() ? fail(ErrorCode::Cancelled, "hold") : Result<void>{};
    };
}

bool waitUntil(const std::function<bool()> &condition, int timeoutMs = 3000)
{
    for (int waited = 0; waited < timeoutMs; waited += 10) {
        if (condition()) {
            return true;
        }
        QTest::qWait(10);
    }
    return condition();
}

} // namespace

TEST_CASE("jobservice: the chip shows the running job with its percent and hides when it is done")
{
    Fixture f;
    const JobsChip chip(f.theme, f.jobs);
    REQUIRE(chip.isHidden());
    std::atomic<bool> release = false;
    const jobs::JobId id = f.jobs.enqueue(jobs::JobKind::StemSeparation, holdAt(0.5, release));
    REQUIRE(waitUntil([&] {
        return f.jobs.current() != nullptr && f.jobs.current()->fraction.has_value();
    }));
    REQUIRE(f.jobs.current()->id == id);
    REQUIRE(f.jobs.current()->state == jobs::JobState::Running);
    REQUIRE_FALSE(chip.isHidden());
    REQUIRE(chip.text().startsWith(QStringLiteral("스템 분리 50 %")));

    release = true;
    REQUIRE(waitUntil([&] {
        return f.jobs.jobs().empty();
    }));
    REQUIRE(f.finishedCount == 1);
    REQUIRE_FALSE(f.lastError);
    REQUIRE(chip.isHidden());
}

TEST_CASE("jobservice: cancel disables the button, says so, and the job finishes as cancelled")
{
    Fixture f;
    const JobsChip chip(f.theme, f.jobs);
    std::atomic<bool> release = false;
    const jobs::JobId first = f.jobs.enqueue(jobs::JobKind::Prediction, holdAt(0.0, release));
    f.jobs.enqueue(jobs::JobKind::Export, holdAt(0.0, release));
    REQUIRE(waitUntil([&] {
        return f.jobs.jobs().size() == 2 && f.jobs.current()->state == jobs::JobState::Running;
    }));
    REQUIRE(chip.text().startsWith(QStringLiteral("작업 2개 · 예측")));

    f.jobs.cancel(first);
    REQUIRE(f.jobs.current()->cancelling);
    REQUIRE(waitUntil([&] {
        return f.finishedCount == 1;
    }));
    REQUIRE(f.lastError);
    REQUIRE(f.lastError.value_or(Error{ErrorCode::DiskFull, {}, {}}).code == ErrorCode::Cancelled);
    REQUIRE(f.jobs.jobs().size() == 1);
    REQUIRE(waitUntil([&] {
        return f.jobs.current()->state == jobs::JobState::Running;
    }));
    REQUIRE(chip.text().startsWith(QStringLiteral("내보내기")));

    f.jobs.cancelAll();
    REQUIRE(waitUntil([&] {
        return f.jobs.jobs().empty();
    }));
    REQUIRE(f.finishedCount == 2);
}
