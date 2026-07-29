add_executable(ChessAssist main.cpp)

target_include_directories(ChessAssist PRIVATE ${INI_CPP_INCLUDE_DIRS})
target_link_libraries(ChessAssist PRIVATE ${OpenCV_LIBS} spdlog::spdlog)

add_custom_command(TARGET ChessAssist POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    "${STOCKFISH_BIN}"
    "$<TARGET_FILE_DIR:ChessAssist>"
    COMMENT "Copying Stockfish binary to output directory..."
)