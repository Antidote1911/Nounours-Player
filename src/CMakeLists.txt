cmake_minimum_required(VERSION 3.16)

project(nounours-player
    VERSION 2.5.3
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/ui)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network WebSockets Svg Widgets LinguistTools)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED mpv)
pkg_check_modules(MEDIAINFO REQUIRED libmediainfo)

set(SOURCES
    main.cpp
    gesturehandler.cpp
    jellyfinmanager.cpp
    mediainfohelper.cpp
    mpvhandler.cpp
    nounourscommands.cpp
    nounoursengine.cpp
    overlay.cpp
    overlayhandler.cpp
    settings.cpp
    updatemanager.cpp
    util.cpp
    versions/2_0_3.cpp
    ui/aboutdialog.cpp
    ui/inputdialog.cpp
    ui/jellyfinresultswidget.cpp
    ui/jellyfinsearchdialog.cpp
    ui/jumpdialog.cpp
    ui/keydialog.cpp
    ui/locationdialog.cpp
    ui/mainwindow.cpp
    ui/preferencesdialog.cpp
    ui/videoequalizer.cpp
    ui/mediainfodialog.cpp
    ui/screenshotdialog.cpp
    ui/updatedialog.cpp
    widgets/customlabel.cpp
    widgets/customlineedit.cpp
    widgets/customslider.cpp
    widgets/customsplitter.cpp
    widgets/dimdialog.cpp
    widgets/indexbutton.cpp
    widgets/openbutton.cpp
    widgets/playlistwidget.cpp
    widgets/seekbar.cpp
)

set(RESOURCES rsclist.qrc)

set(TS_FILES
    translations/nounours-player_de.ts
    translations/nounours-player_es.ts
    translations/nounours-player_fr.ts
    translations/nounours-player_hr.ts
    translations/nounours-player_it.ts
    translations/nounours-player_ka.ts
    translations/nounours-player_ko.ts
    translations/nounours-player_nl.ts
    translations/nounours-player_pt_BR.ts
    translations/nounours-player_ru.ts
    translations/nounours-player_tr.ts
    translations/nounours-player_vi.ts
    translations/nounours-player_zh_CN.ts
)

# ── Platform-specific ────────────────────────────────────────────────────────
if(WIN32)
    pkg_check_modules(LIBZIP REQUIRED libzip)
    list(APPEND SOURCES platform/win.cpp)
    list(APPEND RESOURCES win_rsclist.qrc)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        list(APPEND SOURCES platform/win64.cpp)
    else()
        list(APPEND SOURCES platform/win32.cpp)
    endif()
    configure_file(nounours-player.rc.in "${CMAKE_CURRENT_BINARY_DIR}/nounours-player.rc")
    list(APPEND SOURCES "${CMAKE_CURRENT_BINARY_DIR}/nounours-player.rc")
elseif(APPLE)
    list(APPEND SOURCES platform/osx.cpp)
elseif(UNIX)
    pkg_check_modules(X11 REQUIRED x11)
    list(APPEND SOURCES platform/linux.cpp)
endif()

# ── Translations (.ts → .qm) ─────────────────────────────────────────────────
qt6_add_translation(QM_FILES ${TS_FILES})
add_custom_target(translations ALL DEPENDS ${QM_FILES})

# ── Executable ───────────────────────────────────────────────────────────────
if(WIN32)
    add_executable(nounours-player WIN32 ${SOURCES} ${RESOURCES})
elseif(APPLE)
    add_executable(nounours-player MACOSX_BUNDLE ${SOURCES} ${RESOURCES})
    set_source_files_properties(img/logo.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(nounours-player PROPERTIES MACOSX_BUNDLE_ICON_FILE logo.icns)
else()
    add_executable(nounours-player ${SOURCES} ${RESOURCES})
endif()

add_dependencies(nounours-player translations)

# ── Include paths ────────────────────────────────────────────────────────────
target_include_directories(nounours-player PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets
    ${CMAKE_CURRENT_BINARY_DIR}
    ${MPV_INCLUDE_DIRS}
)

# ── Link libraries ───────────────────────────────────────────────────────────
target_link_libraries(nounours-player PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::WebSockets
    Qt6::Svg
    Qt6::Widgets
    ${MPV_LINK_LIBRARIES}
    ${MEDIAINFO_LINK_LIBRARIES}
)
target_include_directories(nounours-player PRIVATE
    ${MEDIAINFO_INCLUDE_DIRS}
)
target_compile_options(nounours-player PRIVATE ${MEDIAINFO_CFLAGS_OTHER})

if(WIN32)
    target_link_libraries(nounours-player PRIVATE ${LIBZIP_LINK_LIBRARIES})
    target_include_directories(nounours-player PRIVATE ${LIBZIP_INCLUDE_DIRS})
    target_compile_options(nounours-player PRIVATE -pthread)
    target_link_options(nounours-player PRIVATE -pthread)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(nounours-player PRIVATE ${X11_LINK_LIBRARIES})
    target_include_directories(nounours-player PRIVATE ${X11_INCLUDE_DIRS})
endif()

# ── Build-time defines ───────────────────────────────────────────────────────
set(SETTINGS_FILE "nounoursplayer" CACHE STRING "Settings file name")
set(NOUNOURS_LANG_PATH "" CACHE STRING "Runtime path for .qm translation files")

target_compile_definitions(nounours-player PRIVATE
    NOUNOURS_PLAYER_VERSION="${PROJECT_VERSION}"
    SETTINGS_FILE="${SETTINGS_FILE}"
    NOUNOURS_PLAYER_LANG_PATH="${NOUNOURS_LANG_PATH}"
)

# ── Install ──────────────────────────────────────────────────────────────────
include(GNUInstallDirs)

install(TARGETS nounours-player
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE  DESTINATION .
)

install(FILES ${QM_FILES}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/nounours-player/translations
)

if(UNIX AND NOT APPLE)
    install(FILES ../etc/logo/nounours-player.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps
    )
    install(FILES ../etc/logo/nounours-player.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    )
    install(FILES ../etc/nounours-player.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )
    install(FILES ../LICENSE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/nounours-player
    )
endif()
