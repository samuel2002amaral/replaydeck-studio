# ReplayDeck Studio: o programa (interface própria) sobre o motor libobs.
# Compilado dentro da árvore do OBS (tools/patch-obs.py inclui esta pasta) ou separado, contra o libobs instalado.
cmake_minimum_required(VERSION 3.22)

if(NOT TARGET OBS::libobs)
  project(replaydeck-studio-app VERSION 2.0.0 LANGUAGES C CXX)
  find_package(libobs REQUIRED)
endif()

set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

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
  src/position-editor.cpp src/position-editor.hpp
  src/main-window.cpp src/main-window.hpp
  src/replay-host.cpp src/replay-host.hpp
  src/audio-mixer.cpp src/audio-mixer.hpp
  src/outputs.cpp src/outputs.hpp
  src/settings-dialog.cpp src/settings-dialog.hpp
  src/external-output.cpp src/external-output.hpp
  src/virtualcam.cpp src/virtualcam.hpp
  src/functions.cpp src/functions.hpp
  src/vertical-canvas.cpp src/vertical-canvas.hpp
  src/shortcuts.cpp src/shortcuts.hpp
  src/style.hpp
  src/gt-host.cpp src/gt-host.hpp src/gt-stinger.cpp
  src/gpu.cpp src/gpu.hpp
  src/perf.cpp src/perf.hpp
  src/scripts.cpp src/scripts.hpp
  src/interact-window.cpp src/interact-window.hpp
  src/multicorder.cpp src/multicorder.hpp
  src/filters-window.cpp src/filters-window.hpp
  src/plugins.cpp src/plugins.hpp
  src/playlist.cpp src/playlist.hpp
  src/ptz.cpp src/ptz.hpp
  src/vmix-keys.cpp src/vmix-keys.hpp
  src/vmix-api.cpp src/vmix-api.hpp
  src/transitions-panel.cpp src/transitions-panel.hpp
  # replay (veio do plugin ReplayDeck; roda dentro do programa)
  replay/engine.cpp replay/engine.hpp
  replay/recording.cpp replay/recording.hpp
  replay/camera-recorder.cpp replay/camera-recorder.hpp
  replay/audio-recorder.cpp replay/audio-recorder.hpp
  replay/playback-channel.cpp replay/playback-channel.hpp
  replay/replay-source.cpp
  replay/exporter.cpp
  replay/replay-dock.cpp replay/replay-dock.hpp
  replay/setup-wizard.cpp replay/setup-wizard.hpp
  replay/air-control.cpp replay/air-control.hpp
  replay/overlay-transition.cpp replay/overlay-transition.hpp
  replay/ui-theme.cpp replay/ui-theme.hpp
  replay/i18n.cpp replay/i18n.hpp
  replay/studio-host.hpp replay/session.hpp replay/common.hpp
  replay/shim/obs-frontend-api.h
  # títulos GT (vMix .gtzip): cada título é uma entrada
  replay/overlay-channels.hpp
  replay/gt/gt-doc.cpp replay/gt/gt-doc.hpp
  replay/gt/gt-render.cpp replay/gt/gt-render.hpp
  replay/gt/gt-source.cpp replay/gt/gt-source.hpp
  replay/gt/gt-editor.cpp
  replay/gt/gt-dock.cpp replay/gt/gt-dock.hpp
  replay/gt/gt-convert.cpp
  replay/third_party/miniz/miniz.c replay/third_party/miniz/miniz_zip.c
  replay/third_party/miniz/miniz_tdef.c replay/third_party/miniz/miniz_tinfl.c)

set_target_properties(replaydeck-studio PROPERTIES
  OUTPUT_NAME "ReplayDeckStudio"
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  FOLDER frontend
  COMPILE_WARNING_AS_ERROR OFF)
target_compile_definitions(replaydeck-studio PRIVATE RDS_VERSION="${RDS_VERSION}")
target_link_libraries(replaydeck-studio PRIVATE OBS::libobs Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)

# o replay fala com a "interface do OBS" que o próprio Studio responde (replay/shim vem antes de tudo)
target_include_directories(replaydeck-studio BEFORE PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/replay/shim" "${CMAKE_CURRENT_SOURCE_DIR}/replay"
  "${CMAKE_CURRENT_SOURCE_DIR}/replay/third_party/miniz")

# FFmpeg do replay (o mesmo que acompanha o motor: obs-deps no Windows, pacotes do sistema no Linux)
find_path(RDS_FFMPEG_INCLUDE libavcodec/avcodec.h REQUIRED)
find_library(RDS_AVCODEC NAMES avcodec REQUIRED)
find_library(RDS_AVFORMAT NAMES avformat REQUIRED)
find_library(RDS_AVUTIL NAMES avutil REQUIRED)
target_include_directories(replaydeck-studio PRIVATE "${RDS_FFMPEG_INCLUDE}")
target_link_libraries(replaydeck-studio PRIVATE "${RDS_AVCODEC}" "${RDS_AVFORMAT}" "${RDS_AVUTIL}")
target_compile_definitions(replaydeck-studio PRIVATE PLUGIN_VERSION="${RDS_VERSION}" PLUGIN_BUILD="studio")
install(DIRECTORY replay/data/ DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/replaydeck-studio/replay" OPTIONAL)

if(MSVC)
  target_compile_options(replaydeck-studio PRIVATE /utf-8 /W3)
  target_compile_definitions(replaydeck-studio PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
  # otimização máxima também na versão com símbolos (RelWithDebInfo só inline parcial por padrão):
  # inline completo, intrínsecos e otimização do programa inteiro na ligação
  target_compile_options(replaydeck-studio PRIVATE $<$<NOT:$<CONFIG:Debug>>:/Ob2 /Oi /GL /Gy>)
  target_link_options(replaydeck-studio PRIVATE $<$<NOT:$<CONFIG:Debug>>:/LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO>)
  target_link_libraries(replaydeck-studio PRIVATE dxgi winmm)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/res/app.rc")
    target_sources(replaydeck-studio PRIVATE res/app.rc)
  endif()
else()
  target_compile_options(replaydeck-studio PRIVATE -Wall -Wextra)
endif()

if(APPLE)
  # Mac: o programa vai dentro do .app, ao lado dos frameworks do motor e do Qt (Contents/Frameworks)
  set_target_properties(replaydeck-studio PROPERTIES
    BUILD_RPATH "@executable_path/../Frameworks"
    INSTALL_RPATH "@executable_path/../Frameworks")
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
  add_executable(engine-share-test tests/engine-share-test.cpp src/engine.cpp src/engine.hpp src/props-cache.cpp src/gpu.cpp)
  target_link_libraries(engine-share-test PRIVATE OBS::libobs Qt6::Core Qt6::Gui Qt6::Widgets)
  target_compile_definitions(engine-share-test PRIVATE RDS_VERSION="teste")
  set_target_properties(engine-share-test PROPERTIES CXX_STANDARD 17 AUTOMOC ON)
  add_executable(engine-enable-test tests/engine-enable-test.cpp src/engine.cpp src/engine.hpp src/props-cache.cpp src/gpu.cpp)
  target_link_libraries(engine-enable-test PRIVATE OBS::libobs Qt6::Core Qt6::Gui Qt6::Widgets)
  target_compile_definitions(engine-enable-test PRIVATE RDS_VERSION="teste")
  set_target_properties(engine-enable-test PROPERTIES CXX_STANDARD 17 AUTOMOC ON)
  add_executable(engine-layers-test tests/engine-layers-test.cpp src/engine.cpp src/engine.hpp src/props-cache.cpp src/gpu.cpp)
  target_link_libraries(engine-layers-test PRIVATE OBS::libobs Qt6::Core Qt6::Gui Qt6::Widgets)
  target_compile_definitions(engine-layers-test PRIVATE RDS_VERSION="teste")
  set_target_properties(engine-layers-test PROPERTIES CXX_STANDARD 17 AUTOMOC ON)
  add_executable(engine-audio-test tests/engine-audio-test.cpp src/engine.cpp src/engine.hpp src/props-cache.cpp src/gpu.cpp)
  target_link_libraries(engine-audio-test PRIVATE OBS::libobs Qt6::Core Qt6::Gui Qt6::Widgets)
  target_compile_definitions(engine-audio-test PRIVATE RDS_VERSION="teste")
  set_target_properties(engine-audio-test PROPERTIES CXX_STANDARD 17 AUTOMOC ON)
endif()
