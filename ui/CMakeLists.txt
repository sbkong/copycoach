# cc_ui: Qt Widgets layer (theme, shell, instrument views, routine workspace). Depends on cc_core and the
# generated headers; the app target is the only thing above it.
set(CC_UI_SOURCES
    theme/theme.h
    theme/theme.cpp
    theme/palette.cpp
    theme/style.h
    theme/style.cpp
    theme/icons.h
    theme/icons.cpp
    shell/apppaths.h
    shell/apppaths.cpp
    shell/errortext.h
    shell/errortext.cpp
    shell/settings.h
    shell/settings.cpp
    shell/actionregistry.h
    shell/actionregistry.cpp
    shell/panelframe.h
    shell/panelframe.cpp
    shell/shell.h
    shell/shell.cpp
    shell/shellmenus.cpp
    audio/audioservice.h
    audio/audioservice.cpp
    fretboard/fretboardview.h
    fretboard/fretboardview.cpp
    keyboard/keyboardstrip.h
    keyboard/keyboardstrip.cpp
    routine/slotbuilder.h
    routine/slotbuilder.cpp
    routine/routinesession.h
    routine/routinesession.cpp
    routine/routinetimer.h
    routine/routinetimer.cpp
    routine/instrumentpanel.h
    routine/instrumentpanel.cpp
    routine/chordstrip.h
    routine/chordstrip.cpp
    routine/timelineview.h
    routine/timelineview.cpp
    routine/navigatorpanel.h
    routine/navigatorpanel.cpp
    routine/inspectorpanel.h
    routine/inspectorpanel.cpp
    prefs/inlinemessage.h
    prefs/inlinemessage.cpp
    prefs/preferencesdialog.h
    prefs/preferencesdialog.cpp
    prefs/audiodevicepage.h
    prefs/audiodevicepage.cpp
    prefs/appearancepage.h
    prefs/appearancepage.cpp
    prefs/practicepage.h
    prefs/practicepage.cpp
)
qt_add_library(cc_ui STATIC ${CC_UI_SOURCES})
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/icons/icons.txt" CC_ICON_NAMES)
set(CC_ICON_FILES "")
foreach(name IN LISTS CC_ICON_NAMES)
    list(APPEND CC_ICON_FILES "icons/lucide/${name}.svg")
endforeach()
qt_add_resources(cc_ui "icons" PREFIX "/icons/lucide" BASE "icons/lucide" FILES ${CC_ICON_FILES})
target_include_directories(cc_ui PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_definitions(cc_ui PUBLIC QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
target_link_libraries(cc_ui PUBLIC cc_core cc_generated Qt6::Widgets Qt6::Svg)
cc_lint_sources(${CC_UI_SOURCES})
