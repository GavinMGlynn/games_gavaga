cmake_minimum_required(VERSION 3.28)

project(gavaga
    VERSION     0.1.0
    DESCRIPTION "Gavaga - a Galaga-inspired fixed shooter in C23 + SDL3"
    LANGUAGES   C)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(GAVAGA_USE_SYSTEM_SDL "Use find_package(SDL3) instead of FetchContent" OFF)
option(GAVAGA_SDL_SHARED     "Build the fetched SDL3 as a shared library"     OFF)
option(GAVAGA_WERROR         "Treat compiler warnings as errors"              OFF)
option(GAVAGA_TESTS          "Build the unit tests"                           ON)
option(GAVAGA_ASAN           "Build with address+UB sanitizers"               OFF)

set(GAVAGA_SDL_TAG "release-3.2.30" CACHE STRING
    "Git tag of SDL3 to fetch (pinned to the 3.2.x release line)")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # so clangd/IDEs see -std=c23

# ---------------------------------------------------------------------------
# Compiler gate - C23 needs a recent toolchain.
#   GCC 14+   Clang 19+   AppleClang 16+ (Xcode 16)   MSVC 19.39+ (VS 17.9)
# Ubuntu 22.04's default GCC 11 and RHEL 9's default GCC 11 are both too old;
# install gcc-toolset-14 (RHEL/Rocky) or gcc-14 from the toolchain PPA (Ubuntu).
# ---------------------------------------------------------------------------
set(_gv_min "")
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(_gv_min 14)
elseif(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set(_gv_min 19)
elseif(CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    set(_gv_min 16)
elseif(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    set(_gv_min 19.39)
endif()

if(_gv_min AND CMAKE_C_COMPILER_VERSION VERSION_LESS _gv_min)
    message(FATAL_ERROR
        "gavaga needs C23 support.\n"
        "  found:    ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}\n"
        "  required: ${CMAKE_C_COMPILER_ID} ${_gv_min} or newer\n"
        "Point CMake at a newer compiler, e.g.\n"
        "  cmake -B build -DCMAKE_C_COMPILER=gcc-14")
endif()
message(STATUS "gavaga: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION} (C23)")

# MSVC has no /std:c23 yet; its newest C features sit behind /std:clatest, and
# /Zc:__STDC__ makes it report standard version macros honestly. CMake's
# C_STANDARD 23 should already select the former, but say it outright so the
# build does not depend on that mapping staying put.
set(GAVAGA_C_FLAGS "")
if(MSVC)
    set(GAVAGA_C_FLAGS /std:clatest /Zc:__STDC__)
endif()

# Even so, C23 support is uneven: a compiler can take the flag and still not
# implement a given feature. Probe for `nullptr` rather than guessing from
# version numbers, using the same flags the real build gets, so a compiler that
# gains it later simply stops needing the shim.
include(CheckCSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "${GAVAGA_C_FLAGS}")
list(JOIN CMAKE_REQUIRED_FLAGS " " CMAKE_REQUIRED_FLAGS)
check_c_source_compiles(
    "int main(void) { void *p = nullptr; return p != (void *)0; }"
    GAVAGA_HAVE_NULLPTR)
unset(CMAKE_REQUIRED_FLAGS)

if(GAVAGA_HAVE_NULLPTR)
    message(STATUS "gavaga: C23 nullptr - native")
else()
    message(STATUS "gavaga: C23 nullptr - not implemented, using a compatibility macro")
endif()

# ---------------------------------------------------------------------------
# SDL3
# ---------------------------------------------------------------------------
if(GAVAGA_USE_SYSTEM_SDL)
    find_package(SDL3 3.2 REQUIRED CONFIG)
    set(GAVAGA_SDL_TARGET SDL3::SDL3)
    message(STATUS "gavaga: using system SDL3 ${SDL3_VERSION}")
else()
    include(FetchContent)

    if(GAVAGA_SDL_SHARED)
        set(SDL_SHARED ON  CACHE BOOL "" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    else()
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON  CACHE BOOL "" FORCE)
    endif()
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS        OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES     OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL      OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(SDL3
        GIT_REPOSITORY  https://github.com/libsdl-org/SDL.git
        GIT_TAG         ${GAVAGA_SDL_TAG}
        GIT_SHALLOW     TRUE
        GIT_PROGRESS    TRUE
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(SDL3)

    if(GAVAGA_SDL_SHARED)
        set(GAVAGA_SDL_TARGET SDL3::SDL3-shared)
    else()
        set(GAVAGA_SDL_TARGET SDL3::SDL3-static)
    endif()
    message(STATUS "gavaga: fetched SDL3 ${GAVAGA_SDL_TAG} -> ${GAVAGA_SDL_TARGET}")
endif()

# ---------------------------------------------------------------------------
# Everything except main.c goes in a library, so the tests can link the same
# code the game runs.
# ---------------------------------------------------------------------------
add_library(gavaga_core STATIC
    src/gv_math.c
    src/gv_path.c
    src/gv_paths.c
    src/gv_star.c
    src/gv_sprite.c
    src/gv_font.c
    src/gv_render.c
    src/gv_game.c
    src/gv_audio.c
    src/gv_score.c
    src/gv_debug.c)

target_include_directories(gavaga_core PUBLIC src)
target_link_libraries(gavaga_core PUBLIC ${GAVAGA_SDL_TARGET})

add_executable(gavaga src/main.c)
target_link_libraries(gavaga PRIVATE gavaga_core)

# Applies the project's warning set, C23 flags and sanitiser choice to a
# target. Keeping it in one place stops the game and the tests drifting apart.
function(gavaga_configure target)
    if(NOT GAVAGA_HAVE_NULLPTR)
        target_compile_definitions(${target} PRIVATE GV_NO_NULLPTR)
    endif()
    target_compile_options(${target} PRIVATE ${GAVAGA_C_FLAGS})

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
        if(GAVAGA_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wshadow -Wconversion -Wsign-conversion
            -Wpointer-arith -Wstrict-prototypes -Wmissing-prototypes
            -Wno-unused-parameter)
        if(GAVAGA_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()

    if(GAVAGA_ASAN AND NOT MSVC)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target}    PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

gavaga_configure(gavaga_core)
gavaga_configure(gavaga)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(GAVAGA_TESTS)
    enable_testing()
    add_executable(gavaga_tests tests/test_gavaga.c)
    target_link_libraries(gavaga_tests PRIVATE gavaga_core)
    gavaga_configure(gavaga_tests)
    add_test(NAME gavaga COMMAND gavaga_tests)
    # No window, no audio device, no gamepads.
    set_tests_properties(gavaga PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
        TIMEOUT 120)
endif()

# Windows: GUI subsystem so no console window pops up. SDL_MAIN_USE_CALLBACKS
# in main.c supplies WinMain via SDL3/SDL_main.h.
set_target_properties(gavaga PROPERTIES
    WIN32_EXECUTABLE  ON
    MACOSX_BUNDLE     OFF
    C_VISIBILITY_PRESET hidden)

# Ship the SDL3 DLL next to the exe when linking shared on Windows.
if(WIN32 AND NOT GAVAGA_USE_SYSTEM_SDL AND GAVAGA_SDL_SHARED)
    add_custom_command(TARGET gavaga POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL3::SDL3-shared> $<TARGET_FILE_DIR:gavaga>)
endif()

install(TARGETS gavaga RUNTIME DESTINATION bin)
