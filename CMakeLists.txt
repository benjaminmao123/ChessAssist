cmake_minimum_required(VERSION 4.0.0)

SET(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(ChessAssist)

find_package(OpenCV CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_path(INI_CPP_INCLUDE_DIRS "ini/ini.h")

if(WIN32)
    set(STOCKFISH_BIN "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Stockfish/stockfish-windows-x86-64-avx2.exe")
else()
    set(STOCKFISH_BIN "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Stockfish/stockfish-ubuntu-x86-64-avx2")
endif()

add_subdirectory(Source)