# cc_core: no Qt, no UI (PLAN 4-2 principle 1). Everything the app computes lives here.
set(CC_CORE_SOURCES
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
)
add_library(cc_core STATIC ${CC_CORE_SOURCES})
target_include_directories(cc_core PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(cc_core PUBLIC cc_options)
cc_lint_sources(${CC_CORE_SOURCES})
