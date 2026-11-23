# ReplayDeck Studio: o programa (interface própria) sobre o motor libobs.
# Compilado dentro da árvore do OBS (tools/patch-obs.py inclui esta pasta) ou separado, contra o libobs instalado.
cmake_minimum_required(VERSION 3.22)

if(NOT TARGET OBS::libobs)
  project(replaydeck-studio-app VERSION 2.0.0 LANGUAGES C CXX)
  find_package(libobs REQUIRED)
endif()

set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

if(NOT DEFINED RDS_VERSION)
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" RDS_VERSION)
  string(STRIP "${RDS_VERSION}" RDS_VERSION)
endif()

add_executable(replaydeck-studio WIN32
  src/main.cpp
  src/engine.cpp src/engine.hpp
  src/video-display.cpp src/video-display.hpp
  src/properties-view.cpp src/properties-view.hpp
  src/props-cache.cpp src/props-cache.hpp
  src/input-tile.cpp src/input-tile.hpp
  src/dialogs.cpp src/dialogs.hpp
  src/main-window.cpp src/main-window.hpp
  src/style.hpp)

set_target_properties(replaydeck-studio PROPERTIES
  OUTPUT_NAME "ReplayDeckStudio"
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  FOLDER frontend
  COMPILE_WARNING_AS_ERROR OFF)
target_compile_definitions(replaydeck-studio PRIVATE RDS_VERSION="${RDS_VERSION}")
target_link_libraries(replaydeck-studio PRIVATE OBS::libobs Qt6::Core Qt6::Gui Qt6::Widgets)

if(MSVC)
  target_compile_options(replaydeck-studio PRIVATE /utf-8 /W3)
  target_compile_definitions(replaydeck-studio PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/res/app.rc")
    target_sources(replaydeck-studio PRIVATE res/app.rc)
  endif()
else()
  target_compile_options(replaydeck-studio PRIVATE -Wall -Wextra)
endif()

if(DEFINED OBS_EXECUTABLE_DESTINATION)
  # dentro da árvore do OBS: fica em bin/64bit junto do motor e das DLLs do Qt
  install(TARGETS replaydeck-studio RUNTIME DESTINATION "${OBS_EXECUTABLE_DESTINATION}" COMPONENT Runtime)
else()
  install(TARGETS replaydeck-studio RUNTIME DESTINATION bin)
endif()

option(RDS_BUILD_TESTS "Testes locais (Linux)" OFF)
if(RDS_BUILD_TESTS)
  add_executable(props-reload-test tests/props-reload-test.cpp src/properties-view.cpp src/properties-view.hpp src/props-cache.cpp)
  target_link_libraries(props-reload-test PRIVATE OBS::libobs Qt6::Core Qt6::Gui Qt6::Widgets)
  set_target_properties(props-reload-test PROPERTIES CXX_STANDARD 17 AUTOMOC ON)
  add_executable(props-cache-test tests/props-cache-test.cpp src/props-cache.cpp src/props-cache.hpp src/properties-view.cpp src/properties-view.hpp)
  target_link_libraries(props-cache-test PRIVATE OBS::libobs Qt6::Core Qt6::Gui Qt6::Widgets)
  set_target_properties(props-cache-test PROPERTIES CXX_STANDARD 17 AUTOMOC ON)
endif()
