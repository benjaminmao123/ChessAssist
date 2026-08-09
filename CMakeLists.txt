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

add_subdirectory(Vendor)
add_subdirectory(Source)

option(BUILD_TESTS "Build tests" ON)

if (BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()