find_package(GTest CONFIG REQUIRED)

add_executable(ChessAssistTests
    UCIProtocolTests.cpp
    EngineControllerTests.cpp
    ChessRulesTests.cpp
    MoveGeneratorTests.cpp
    FenWriterTests.cpp
    PolyglotBookTests.cpp
    GameSessionTests.cpp
    AnalysisBoardSessionTests.cpp
    BrowserPipelineTests.cpp
    SettingsIniTests.cpp
)

target_link_libraries(ChessAssistTests PRIVATE ChessAssistCore GTest::gtest_main GTest::gtest)

add_dependencies(ChessAssistTests build_stockfish)

# EngineControllerTests spawns the real Stockfish binary; EngineController looks for it via
# ExecutablePathUtil::GetDefaultEnginePath() (Assets/Engine/, relative to its own running
# executable), so mirror that same layout next to ChessAssistTests too (it otherwise only ends
# up next to ChessAssist).
add_custom_command(TARGET ChessAssistTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:ChessAssistTests>/Assets/Engine"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE_DIR:ChessAssist>/Assets/Engine/stockfish${CMAKE_EXECUTABLE_SUFFIX}"
        "$<TARGET_FILE_DIR:ChessAssistTests>/Assets/Engine/stockfish${CMAKE_EXECUTABLE_SUFFIX}"
)

# Local HTML fixtures for BrowserPipelineTests.
add_custom_command(TARGET ChessAssistTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/Fixtures"
        "$<TARGET_FILE_DIR:ChessAssistTests>/Fixtures"
)

include(GoogleTest)
gtest_discover_tests(ChessAssistTests)
