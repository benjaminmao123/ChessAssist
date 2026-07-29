add_executable(ChessAssist main.cpp)

target_include_directories(ChessAssist PRIVATE ${INI_CPP_INCLUDE_DIRS})
target_link_libraries(ChessAssist PRIVATE ${OpenCV_LIBS} spdlog::spdlog)