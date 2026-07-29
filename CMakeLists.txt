cmake_minimum_required(VERSION 4.0.0)

SET(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(ChessAssist)

find_package(OpenCV CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_path(INI_CPP_INCLUDE_DIRS "ini/ini.h")

add_subdirectory(Source)