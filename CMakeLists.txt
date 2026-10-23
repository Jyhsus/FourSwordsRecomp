cmake_minimum_required(VERSION 3.20)
project(FourSwordsRecomp VERSION 0.0.1 LANGUAGES C CXX)

# FourSwordsRecomp — native recompilation of The Legend of Zelda: Four Swords
# (from the USA "A Link to the Past & Four Swords" cartridge, AZLE), built on
# the GBARecomp framework. Layout follows the upstream game projects
# (MarioKartSuperCircuitRecomp / MinishCapRecomp).

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(ANDROID)
    set(_netplay_default OFF)
else()
    set(_netplay_default ON)
endif()
option(GBARECOMP_NETPLAY "Build online multiplayer (2-4 players)" ${_netplay_default})
option(GBAGAME_RECOMP_UI "Build the recomp-ui pre-boot launcher" ON)

set(GBARECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/gbarecomp"
    CACHE PATH "Path to this game's gbarecomp checkout")
set(RECOMP_UI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui"
    CACHE PATH "Path to the shared recomp-ui checkout")
# The in-game runtime UI comes from recomp-ui too; a headless build
# (GBAGAME_RECOMP_UI=OFF, e.g. CI or servers without SDL) leaves it out.
if(GBAGAME_RECOMP_UI)
    set(GBARECOMP_RUNTIME_UI_ROOT "${RECOMP_UI_ROOT}"
        CACHE PATH "recomp-ui root used by the live GBA runtime adapter")
else()
    set(GBARECOMP_RUNTIME_UI_ROOT "" CACHE PATH
        "recomp-ui root used by the live GBA runtime adapter" FORCE)
endif()
set(GBARECOMP_SELFHEAL_RECOMPILE_DEFAULT ON CACHE BOOL
    "Enable runtime self-heal sharding by default" FORCE)
set(GBARECOMP_ENABLE_MODS ON CACHE BOOL
    "Enable the mod catalog and GBA mod packages" FORCE)

if(NOT EXISTS "${GBARECOMP_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "gbarecomp submodule missing. Run: git submodule update --init --recursive")
endif()
# Local engine patches waiting for upstream (patches/README.md).
file(READ "${GBARECOMP_ROOT}/src/runtime/runtime.h" _gbarecomp_runtime_h)
if(NOT _gbarecomp_runtime_h MATCHES "request_quit")
    message(FATAL_ERROR
        "gbarecomp lacks a local patch. Run: git -C gbarecomp apply "
        "../patches/gbarecomp/0001-runtime-request-quit.patch (see patches/README.md)")
endif()
file(READ "${GBARECOMP_ROOT}/src/gba/gba_link_hub.h" _gbarecomp_link_hub_h)
if(NOT _gbarecomp_link_hub_h MATCHES "set_multiplayer_transfer_cycles")
    message(FATAL_ERROR
        "gbarecomp lacks a local patch. Run: git -C gbarecomp apply "
        "../patches/gbarecomp/0002-link-hub-transfer-time-override.patch (see patches/README.md)")
endif()
add_subdirectory("${GBARECOMP_ROOT}" gbarecomp_build EXCLUDE_FROM_ALL)

# ── Generated cartridge code (local only, derived from the player's ROM) ──
set(GBA_GAME_GENERATED_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/generated" CACHE PATH
    "Generated native corpus produced by tools/regen.ps1 / tools/regen.sh")
file(GLOB GAME_GENERATED_SOURCES CONFIGURE_DEPENDS
    "${GBA_GAME_GENERATED_ROOT}/*.c"
    "${GBA_GAME_GENERATED_ROOT}/*.cpp")
if(NOT GAME_GENERATED_SOURCES)
    message(FATAL_ERROR
        "No generated code in ${GBA_GAME_GENERATED_ROOT}. Put your ROM in roms/ "
        "and run tools/regen.ps1 (Windows) or tools/regen.sh first.")
endif()

# Generated code + game hooks, shared by the game and the M3 link probe.
add_library(fsr_game OBJECT
    src/fs_boot.cpp
    src/fs_save.cpp
    ${GAME_GENERATED_SOURCES})
target_include_directories(fsr_game PUBLIC
    src
    "${GBARECOMP_ROOT}/src/armv4t"
    "${GBARECOMP_ROOT}/src/gba"
    "${GBARECOMP_ROOT}/src/runtime"
    "${GBARECOMP_ROOT}/src/debug")
# Compile-time usage requirements only; executables link the libraries.
target_link_libraries(fsr_game PRIVATE
    gbarecomp_runtime gbarecomp_debug gbarecomp_gba gbarecomp_armv4t)

add_executable(FourSwordsRecomp
    src/main.cpp
    $<TARGET_OBJECTS:fsr_game>)

# Keep the big generated shards cheap to compile.
set(GAME_RECOMPILED_SOURCES ${GAME_GENERATED_SOURCES})
list(FILTER GAME_RECOMPILED_SOURCES INCLUDE REGEX "/recompiled_[0-9]+\\.cpp$")
if(GAME_RECOMPILED_SOURCES)
    if(MSVC)
        set_source_files_properties(${GAME_RECOMPILED_SOURCES} PROPERTIES
            COMPILE_OPTIONS "/O1")
    else()
        set_source_files_properties(${GAME_RECOMPILED_SOURCES} PROPERTIES
            COMPILE_OPTIONS "-O1;-g0")
    endif()
endif()

target_include_directories(FourSwordsRecomp PRIVATE
    src
    "${GBARECOMP_ROOT}/src/armv4t"
    "${GBARECOMP_ROOT}/src/gba"
    "${GBARECOMP_ROOT}/src/runtime"
    "${GBARECOMP_ROOT}/src/debug")

if(MSVC)
    target_link_libraries(FourSwordsRecomp PRIVATE
        gbarecomp_runtime gbarecomp_debug gbarecomp_gba gbarecomp_armv4t)
else()
    target_link_libraries(FourSwordsRecomp PRIVATE
        "-Wl,--start-group"
        gbarecomp_runtime gbarecomp_debug gbarecomp_gba gbarecomp_armv4t
        "-Wl,--end-group")
endif()

target_compile_definitions(FourSwordsRecomp PRIVATE
    FSR_VERSION="${PROJECT_VERSION}"
    GBARECOMP_DEFAULT_DEBUG_PORT=19890
    GBARECOMP_WINDOW_TITLE="FourSwordsRecomp"
    GBARECOMP_DEFAULT_GAME_CONFIG="game.toml")

if(GBARECOMP_NETPLAY)
    target_link_libraries(FourSwordsRecomp PRIVATE gbarecomp_netplay)
    gbarecomp_target_netplay_identity(FourSwordsRecomp
        "${CMAKE_CURRENT_SOURCE_DIR}" "${GBA_GAME_GENERATED_ROOT}")
    set_property(SOURCE src/main.cpp APPEND PROPERTY
        COMPILE_DEFINITIONS GBAGAME_NETPLAY=1)
endif()

# ── Launcher (recomp-ui): ROM/BIOS picker with SHA-1 check, settings, netplay lobby ──
if(GBAGAME_RECOMP_UI)
    include(${RECOMP_UI_ROOT}/recomp_ui.cmake)
    # The file screen (section 8.1) uses the launcher's SDL2 + ImGui build.
    add_library(game_launcher_ui STATIC src/game_launcher_boot.cpp src/fs_slots_ui.cpp)
    target_include_directories(game_launcher_ui PRIVATE
        "${GBARECOMP_ROOT}/src/runtime" "${GBARECOMP_ROOT}/src/gba" src)
    target_compile_definitions(game_launcher_ui PRIVATE GBARECOMP_ENABLE_MODS=1)
    recomp_target_launcher_ui(game_launcher_ui CONSOLE gba)
    if(GBARECOMP_NETPLAY)
        gbarecomp_target_netplay_launcher(game_launcher_ui)
    endif()
    find_package(SDL2 REQUIRED)
    target_link_libraries(game_launcher_ui PUBLIC SDL2::SDL2)
    if(WIN32)
        target_link_libraries(game_launcher_ui PUBLIC opengl32)
    endif()
    target_link_libraries(FourSwordsRecomp PRIVATE game_launcher_ui)
    # Local split-screen multiplayer (section 10): SDL controllers + HostWindow.
    target_sources(FourSwordsRecomp PRIVATE src/fs_local.cpp)
    set_property(SOURCE src/main.cpp APPEND PROPERTY
        COMPILE_DEFINITIONS GBAGAME_RECOMP_UI)
endif()

# Host-side save model tests (no ROM needed): ctest --test-dir build -R fs_save
include(CTest)
if(BUILD_TESTING)
    add_executable(fs_save_test tests/fs_save_test.cpp src/fs_save.cpp)
    target_include_directories(fs_save_test PRIVATE src)
    add_test(NAME fs_save COMMAND fs_save_test)
endif()

# M3 link probe: 2-4 virtual GBAs on one GbaLinkHub cable in one process, no
# network or window (tools/link_probe.cpp). Not built by default:
#   ninja -C build FourSwordsLinkProbe
add_executable(FourSwordsLinkProbe EXCLUDE_FROM_ALL
    tools/link_probe.cpp
    $<TARGET_OBJECTS:fsr_game>)
target_include_directories(FourSwordsLinkProbe PRIVATE
    src
    "${GBARECOMP_ROOT}/src/armv4t"
    "${GBARECOMP_ROOT}/src/gba"
    "${GBARECOMP_ROOT}/src/runtime"
    "${GBARECOMP_ROOT}/src/debug")
if(MSVC)
    target_link_libraries(FourSwordsLinkProbe PRIVATE
        gbarecomp_runtime gbarecomp_debug gbarecomp_gba gbarecomp_armv4t)
else()
    target_link_libraries(FourSwordsLinkProbe PRIVATE
        "-Wl,--start-group"
        gbarecomp_runtime gbarecomp_debug gbarecomp_gba gbarecomp_armv4t
        "-Wl,--end-group")
endif()
# The runtime library references the in-game UI and netplay when built in.
if(GBARECOMP_NETPLAY)
    target_link_libraries(FourSwordsLinkProbe PRIVATE gbarecomp_netplay)
endif()
if(GBAGAME_RECOMP_UI)
    target_link_libraries(FourSwordsLinkProbe PRIVATE game_launcher_ui)
endif()

foreach(_exe FourSwordsRecomp FourSwordsLinkProbe)
    if(MSVC)
        target_link_options(${_exe} PRIVATE /STACK:16777216)
    elseif(MINGW OR WIN32)
        target_link_options(${_exe} PRIVATE -Wl,--stack,16777216)
    endif()
endforeach()
