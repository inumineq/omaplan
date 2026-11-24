cmake_minimum_required(VERSION 3.22)
# The one place for the version: --version, the MCP serverInfo and the release
# tag (the recipe in omarchy-pkgs follows the tag).
project(omaplan VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2 Sql Test LinguistTools)
# Omarchy's font is fontconfig's monospace (src/theme.cpp).
find_package(PkgConfig REQUIRED)
pkg_check_modules(Fontconfig REQUIRED IMPORTED_TARGET fontconfig)

# Everything but main.cpp, shared by the app and the tests, so each source
# compiles once and a new file in src/ needs no line here.
file(GLOB omaplan_sources CONFIGURE_DEPENDS src/*.cpp src/*.h)
list(REMOVE_ITEM omaplan_sources ${CMAKE_SOURCE_DIR}/src/main.cpp)
add_library(omaplan_core STATIC ${omaplan_sources})
target_include_directories(omaplan_core PUBLIC src)
target_link_libraries(omaplan_core PUBLIC
  Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Sql PkgConfig::Fontconfig)
target_compile_options(omaplan_core PUBLIC -Wall -Wextra)
target_compile_definitions(omaplan_core PUBLIC OMAPLAN_VERSION="${PROJECT_VERSION}")

# The QML, the AI schema, the agent skill and the read-only page, compiled
# into every binary that loads them.
set(omaplan_resources src/resources.qrc src/ai-schema.qrc src/skill.qrc src/web.qrc)
file(GLOB omaplan_translations CONFIGURE_DEPENDS i18n/omaplan_*.ts)

add_executable(omaplan src/main.cpp ${omaplan_resources})
target_link_libraries(omaplan PRIVATE omaplan_core)
qt_add_translations(omaplan TS_FILES ${omaplan_translations} RESOURCE_PREFIX /i18n LUPDATE_TARGET omaplan_lupdate)

# The tests run the real binary (build/omaplan) and read the fixtures from the
# source tree, so they live one directory below it, in build/tests.
enable_testing()
function(omaplan_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE omaplan_core Qt6::Test)
  target_compile_definitions(${name} PRIVATE
    OMAPLAN_SRC="${CMAKE_SOURCE_DIR}/src"
    OMAPLAN_I18N="${CMAKE_SOURCE_DIR}/i18n"
    FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/ai-fixtures")
  set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
  add_dependencies(${name} omaplan)
  add_test(NAME ${name} COMMAND ${name})
  # A desktop platform theme (gtk3) would need a display.
  set_tests_properties(${name} PROPERTIES
    ENVIRONMENT QT_QPA_PLATFORM=offscreen
    ENVIRONMENT_MODIFICATION QT_QPA_PLATFORMTHEME=unset:)
endfunction()

# Store, actions, CLI, MCP, models, dates, theme and translations.
omaplan_test(omaplan_tests tests/omaplan_tests.cpp ${omaplan_resources})
qt_add_translations(omaplan_tests TS_FILES ${omaplan_translations} RESOURCE_PREFIX /i18n LUPDATE_TARGET omaplan_tests_lupdate)
# The AI layer with fake engines; no test calls a real AI.
omaplan_test(ai-unit-tests tests/unit/tst_ai.cpp src/ai-schema.qrc)
# The settings panel in the real window, with a fake engine.
omaplan_test(panel-tests tests/panel/tst_panel.cpp ${omaplan_resources})
qt_add_translations(panel-tests TS_FILES ${omaplan_translations} RESOURCE_PREFIX /i18n LUPDATE_TARGET panel_tests_lupdate)

# Every UI string has a translation in each i18n/omaplan_*.ts.
add_test(NAME i18n-check COMMAND ${CMAKE_SOURCE_DIR}/bin/i18n-check)
# A changed skill lists main's text as released (src/skill.cpp).
add_test(NAME skill-released COMMAND ${CMAKE_SOURCE_DIR}/tests/skill-released)
# The read-only page's JavaScript against the same reference pages. Node is a
# check dependency: without it this fails rather than skips.
add_test(NAME web COMMAND node ${CMAKE_SOURCE_DIR}/web/test.mjs)

# Manual evaluation of a real engine against the fixtures (never a test).
add_executable(ai-eval tests/ai/eval.cpp src/ai-schema.qrc)
target_link_libraries(ai-eval PRIVATE omaplan_core)
target_compile_definitions(ai-eval PRIVATE FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/ai-fixtures")
set_target_properties(ai-eval PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)

# `cmake --install build --prefix ~/.local` puts the app in the launcher
# before it is packaged; a package installs the same files under /usr.
include(GNUInstallDirs)
install(TARGETS omaplan RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES share/omaplan.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES share/omaplan.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/omaplan)
