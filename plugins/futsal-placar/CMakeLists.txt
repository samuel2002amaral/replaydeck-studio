cmake_minimum_required(VERSION 3.22)
project(futsal-placar VERSION 1.0.0 LANGUAGES C CXX)

# Dentro da árvore do OBS (sistema completo) ou separado, contra o SDK instalado (teste/Linux).
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(TARGET OBS::libobs)
  set(PLACAR_IN_TREE ON)
  set(PLACAR_OBS_LIBS OBS::libobs OBS::frontend-api)
else()
  find_package(libobs REQUIRED)
  find_package(obs-frontend-api REQUIRED)
  set(PLACAR_OBS_LIBS OBS::libobs OBS::obs-frontend-api)
endif()
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_library(futsal-placar MODULE
  src/plugin-main.cpp
  src/state.cpp src/state.hpp
  src/placar-source.cpp
  src/placar-dock.cpp src/placar-dock.hpp)

target_link_libraries(futsal-placar PRIVATE ${PLACAR_OBS_LIBS} Qt6::Core Qt6::Gui Qt6::Widgets)
set_target_properties(futsal-placar PROPERTIES AUTOMOC ON PREFIX "" COMPILE_WARNING_AS_ERROR OFF)

# controle remoto pelo obs-websocket (só o cabeçalho; funciona se o obs-websocket estiver no programa)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../obs-websocket/lib/obs-websocket-api.h")
  target_include_directories(futsal-placar PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../obs-websocket/lib")
endif()

if(MSVC)
  target_compile_options(futsal-placar PRIVATE /utf-8 /W3)
  target_compile_definitions(futsal-placar PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(futsal-placar PRIVATE -Wall -Wextra)
endif()

if(PLACAR_IN_TREE)
  # Dentro do ReplayDeck Studio a DLL é copiada para obs-plugins/64bit pelo empacotamento
  # (não entra na lista de módulos "core" do OBS: se falhar, não derruba o programa).
  set_target_properties(futsal-placar PROPERTIES FOLDER plugins)
  return()
endif()

if(WIN32)
  install(TARGETS futsal-placar LIBRARY DESTINATION obs-plugins/64bit RUNTIME DESTINATION obs-plugins/64bit)
  install(DIRECTORY data/ DESTINATION data/obs-plugins/futsal-placar)
else()
  install(TARGETS futsal-placar LIBRARY DESTINATION lib/obs-plugins)
  install(DIRECTORY data/ DESTINATION share/obs/obs-plugins/futsal-placar)
endif()
