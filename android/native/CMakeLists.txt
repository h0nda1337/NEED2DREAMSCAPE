cmake_minimum_required(VERSION 3.16)
project(need2dreamscape_android LANGUAGES C)

if(NOT ANDROID)
    message(FATAL_ERROR "Use the Android NDK toolchain.")
endif()
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${SDL2_SOURCE}" "${CMAKE_BINARY_DIR}/sdl")

set(NOX11 ON CACHE BOOL "" FORCE)
set(STATICLIB ON CACHE BOOL "" FORCE)
set(NO_INIT_CONSTRUCTOR ON CACHE BOOL "" FORCE)
set(DEFAULT_ES 2 CACHE STRING "" FORCE)
set(USE_ANDROID_LOG ON CACHE BOOL "" FORCE)
set(USE_CCACHE OFF CACHE BOOL "" FORCE)
add_subdirectory("${GL4ES_SOURCE}" "${CMAKE_BINARY_DIR}/gl4es")
set_target_properties(GL PROPERTIES POSITION_INDEPENDENT_CODE ON
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/compat")

file(COPY "${SDL2_SOURCE}/include/" DESTINATION "${CMAKE_BINARY_DIR}/include/SDL2")
set(GAME_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../game/src")
add_library(main SHARED "${GAME_SOURCE}/engine.c" "${GAME_SOURCE}/game.c"
    "${GAME_SOURCE}/json.c" "${GAME_SOURCE}/lighting.c"
    "${GAME_SOURCE}/physics.c" "${GAME_SOURCE}/localization.c")
set_target_properties(main PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_compile_definitions(main PRIVATE main=SDL_main)
target_include_directories(main SYSTEM PRIVATE "${CMAKE_BINARY_DIR}/include" "${GL4ES_SOURCE}/include")
target_compile_options(main PRIVATE -Wall -Wextra -Wshadow -Wconversion -pedantic-errors)
target_link_libraries(main PRIVATE SDL2 GL GLESv2 EGL z log android dl m)
