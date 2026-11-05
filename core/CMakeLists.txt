# cc_core: no Qt, no UI (PLAN 4-2 principle 1). Everything the app computes lives here.
find_package(nlohmann_json CONFIG REQUIRED)
find_package(FluidSynth CONFIG REQUIRED)
find_package(portaudio CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(SndFile CONFIG REQUIRED)
find_package(SampleRate CONFIG REQUIRED)

set(CC_CORE_SOURCES
    error.h
    version.h
    version.cpp
    theory/constants.h
    theory/pitch.h
    theory/pitch.cpp
    theory/tuning.h
    theory/tuning.cpp
    theory/chord.h
    theory/chord.cpp
    theory/shellvoicing.h
    theory/shellvoicing.cpp
    theory/scaleform.h
    theory/scaleform.cpp
    theory/triadcycle.h
    theory/triadcycle.cpp
    theory/weekdayplan.h
    theory/weekdayplan.cpp
    theory/chordsymbol.h
    theory/chordsymbol.cpp
    theory/fingering.h
    theory/fingering.cpp
    model/id.h
    model/id.cpp
    model/duration.h
    model/score.h
    model/score.cpp
    model/prediction.h
    model/locate.h
    model/locate.cpp
    model/timemap.h
    model/timemap.cpp
    model/scorebuilder.h
    model/scorebuilder.cpp
    io/limits.h
    io/jsonfile.h
    io/jsonfile.cpp
    io/wavfile.h
    io/wavfile.cpp
    media/audiofile.h
    media/audiofile.cpp
    media/resampler.h
    media/resampler.cpp
    project/uuid.h
    project/uuid.cpp
    project/project.h
    project/project.cpp
    project/jsonread.h
    project/scorejson.h
    project/scorejson.cpp
    project/projectfile.h
    project/projectfile.cpp
    edit/command.h
    edit/undostack.h
    edit/undostack.cpp
    edit/commands.h
    edit/snapshot.h
    edit/snapshot.cpp
    edit/measureops.h
    edit/measureops.cpp
    edit/measurecommands.cpp
    edit/structurecommands.cpp
    edit/projectcommands.cpp
    edit/acceptcommand.cpp
    jobs/job.h
    jobs/jobqueue.h
    jobs/jobqueue.cpp
    score/musicxmlimport.h
    score/musicxmlcontext.h
    score/musicxmlimport.cpp
    score/musicxmlnote.cpp
    score/musicxmlharmony.cpp
    score/meiwriter.h
    score/meicontext.h
    score/meiwriter.cpp
    score/meilayer.cpp
    routine/sequencer.h
    routine/sequencer.cpp
    routine/definition.h
    routine/definition.cpp
    routine/state.h
    routine/state.cpp
    routine/timer.h
    routine/timer.cpp
    stats/event.h
    stats/reader.h
    stats/reader.cpp
    stats/sqlitestore.h
    stats/sqlitestore.cpp
    audio/rt/audioblock.h
    audio/rt/spscqueue.h
    audio/rt/allocguard.h
    audio/rt/allocguard.cpp
    audio/rt/tickclock.h
    audio/rt/transport.h
    audio/rt/transport.cpp
    audio/rt/chunkring.h
    audio/renderworker.h
    audio/renderworker.cpp
    audio/stretcher.h
    audio/stretcher.cpp
    audio/units.h
    audio/command.h
    audio/synth.h
    audio/backend.h
    audio/nullbackend.h
    audio/nullbackend.cpp
    audio/metronome.h
    audio/metronome.cpp
    audio/sequence.h
    audio/sequence.cpp
    audio/engine.h
    audio/engine.cpp
    audio/catalog.h
    audio/catalog.cpp
    audio/fluidsynthnode.h
    audio/fluidsynthnode.cpp
    audio/portaudiobackend.h
    audio/portaudiobackend.cpp
)
add_library(cc_core STATIC ${CC_CORE_SOURCES})
target_include_directories(cc_core PUBLIC ${CMAKE_SOURCE_DIR})
# Third-party engines the copy workflow builds on (copy-spec.md section 16.2 B-5): score engine, inference, video.
target_link_libraries(cc_core PRIVATE cc_verovio cc_ffmpeg)
if(CC_HAVE_ORT)
    target_link_libraries(cc_core PRIVATE cc_onnxruntime)
    target_compile_definitions(cc_core PUBLIC CC_HAVE_ORT=1)
endif()
target_link_libraries(cc_core PUBLIC cc_options nlohmann_json::nlohmann_json PRIVATE FluidSynth::libfluidsynth portaudio unofficial::sqlite3::sqlite3 SndFile::sndfile SampleRate::samplerate)
cc_lint_sources(${CC_CORE_SOURCES})

# Global operator new hook for the allocation guard (coding-standards.md section 8): an OBJECT library so the
# replacement is always linked where it is listed (tests, debug app), and never into cc_core itself.
add_library(cc_allochook OBJECT audio/rt/allochook.cpp)
target_include_directories(cc_allochook PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(cc_allochook PRIVATE cc_options)
cc_lint_sources(audio/rt/allochook.cpp)
