find_package(GTest CONFIG REQUIRED)

add_executable(ChessAssistTests
    SyntheticBoard.h
    SyntheticBoard.cpp
    UCIProtocolTests.cpp
    MoveDetectorTests.cpp
    VisionPipelineTests.cpp
    EngineControllerTests.cpp
    GameSessionTests.cpp
    ReferenceAssetsTests.cpp
)

target_link_libraries(ChessAssistTests PRIVATE ChessAssistCore GTest::gtest_main GTest::gtest)

add_dependencies(ChessAssistTests build_stockfish)

# EngineControllerTests spawns the real Stockfish binary; EngineController looks for it next
# to its own running executable, so copy it next to ChessAssistTests too (it otherwise only
# ends up next to ChessAssist).
add_custom_command(TARGET ChessAssistTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE_DIR:ChessAssist>/stockfish${CMAKE_EXECUTABLE_SUFFIX}"
        "$<TARGET_FILE_DIR:ChessAssistTests>/stockfish${CMAKE_EXECUTABLE_SUFFIX}"
)

# Reference piece-recognition assets, needed by the reference-asset bootstrap test.
add_custom_command(TARGET ChessAssistTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/Assets"
        "$<TARGET_FILE_DIR:ChessAssistTests>/Assets"
)

include(GoogleTest)
gtest_discover_tests(ChessAssistTests)
