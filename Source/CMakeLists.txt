set(LOGGING_SOURCES
    Logging/Log.h
)

set(ENGINE_SOURCES
    Engine/EngineTypes.h
    Engine/ExecutablePathUtil.h
    Engine/ExecutablePathUtil.cpp
    Engine/UCIProtocol.h
    Engine/UCIProtocol.cpp
    Engine/UCIClient.h
    Engine/UCIClient.cpp
    Engine/EngineController.h
    Engine/EngineController.cpp
    Process/ChildProcess.h
)

if (WIN32)
    list(APPEND ENGINE_SOURCES Process/ChildProcess_Win32.cpp)
else()
    list(APPEND ENGINE_SOURCES Process/ChildProcess_Posix.cpp)
endif()

set(CHESS_SOURCES
    Chess/ChessTypes.h
    Chess/ChessTypes.cpp
    Chess/ChessRules.h
    Chess/ChessRules.cpp
)

set(BROWSER_SOURCES
    Browser/BrowserLauncher.h
    Browser/CdpClient.h
    Browser/CdpClient.cpp
    Browser/ChessSiteAdapter.h
    Browser/ChessSiteAdapter.cpp
)

if (WIN32)
    list(APPEND BROWSER_SOURCES Browser/BrowserLauncher_Win32.cpp)
else()
    list(APPEND BROWSER_SOURCES Browser/BrowserLauncher_Posix.cpp)
endif()

set(GAME_SOURCES
    Game/GameTracker.h
    Game/GameTracker.cpp
    Game/MoveListDiff.h
    Game/MoveListDiff.cpp
    Game/GameSession.h
    Game/GameSession.cpp
)

# Library form of the app logic, so both ChessAssist and ChessAssistTests (see ../Tests)
# can link against it without recompiling everything twice.
add_library(ChessAssistCore STATIC ${ENGINE_SOURCES} ${CHESS_SOURCES} ${BROWSER_SOURCES} ${GAME_SOURCES})

target_include_directories(ChessAssistCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ChessAssistCore PUBLIC spdlog::spdlog Threads::Threads ixwebsocket::ixwebsocket nlohmann_json::nlohmann_json)

set(UI_SOURCES
    UI/AppWindow.h
    UI/AppWindow.cpp
    UI/EngineInfoPanel.h
    UI/EngineInfoPanel.cpp
    UI/BoardStatePanel.h
    UI/BoardStatePanel.cpp
    UI/LogPanel.h
    UI/LogPanel.cpp
    UI/ImGuiLogSink.h
)

# UI sources are linked only into the ChessAssist executable, not ChessAssistCore - keeps
# GLFW/OpenGL out of ChessAssistTests, which has no window/GL context to run against.
add_executable(ChessAssist main.cpp ${UI_SOURCES})

target_include_directories(ChessAssist PRIVATE ${INI_CPP_INCLUDE_DIRS})
target_link_libraries(ChessAssist PRIVATE ChessAssistCore imgui::imgui glfw OpenGL::GL)

add_dependencies(ChessAssist build_stockfish)
