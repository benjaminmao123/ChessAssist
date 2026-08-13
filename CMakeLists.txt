cmake_minimum_required(VERSION 4.0.0)

SET(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(ChessAssist)

find_package(spdlog CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(ixwebsocket CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_path(INI_CPP_INCLUDE_DIRS "ini/ini.h")
find_package(Stb REQUIRED)
find_package(nfd CONFIG REQUIRED)

# App version string, embedded into the binary at configure time - sourced from git so it
# always matches whatever tag release.yml built from. Falls back to "unknown" outside a git
# checkout (e.g. a source tarball) or if git isn't available. Re-run cmake configure after
# tagging/committing to pick up a new value; it isn't tracked live during a build.
find_package(Git QUIET)
if (GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE CHESSASSIST_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if (NOT CHESSASSIST_VERSION)
    set(CHESSASSIST_VERSION "unknown")
endif()
message(STATUS "ChessAssist version: ${CHESSASSIST_VERSION}")

configure_file("${CMAKE_SOURCE_DIR}/Source/Version.h.in" "${CMAKE_BINARY_DIR}/generated/Version.h" @ONLY)

add_subdirectory(Vendor)
add_subdirectory(Source)

option(BUILD_TESTS "Build tests" ON)

if (BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()