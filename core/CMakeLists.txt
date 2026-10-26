# cc_core: no Qt, no UI (PLAN 4-2 principle 1). Everything the app computes lives here.
find_package(nlohmann_json CONFIG REQUIRED)

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
    model/id.h
    model/id.cpp
    model/duration.h
    model/score.h
    model/score.cpp
    model/timemap.h
    model/timemap.cpp
    model/scorebuilder.h
    model/scorebuilder.cpp
    io/jsonfile.h
    io/jsonfile.cpp
    routine/sequencer.h
    routine/sequencer.cpp
    routine/definition.h
    routine/definition.cpp
    routine/state.h
    routine/state.cpp
)
add_library(cc_core STATIC ${CC_CORE_SOURCES})
target_include_directories(cc_core PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(cc_core PUBLIC cc_options nlohmann_json::nlohmann_json)
cc_lint_sources(${CC_CORE_SOURCES})
