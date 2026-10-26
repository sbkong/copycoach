# cc_core: no Qt, no UI (PLAN 4-2 principle 1). Everything the app computes lives here.
add_library(cc_core STATIC
    version.h
    version.cpp
    theory/constants.h
)
target_include_directories(cc_core PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(cc_core PUBLIC cc_options)
