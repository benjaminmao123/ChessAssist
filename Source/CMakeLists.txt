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
    Engine/MultiPvCollector.h
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
    Chess/ChessBoardOps.h
    Chess/ChessBoardOps.cpp
    Chess/ChessRules.h
    Chess/ChessRules.cpp
    Chess/MoveGenerator.h
    Chess/MoveGenerator.cpp
    Chess/FenWriter.h
    Chess/FenWriter.cpp
    Chess/PolyglotBook.h
    Chess/PolyglotBook.cpp
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
    Game/AccuracyTracker.h
    Game/AccuracyTracker.cpp
    Game/PremoveTracker.h
    Game/PremoveTracker.cpp
    Game/GameSession.h
    Game/GameSession.cpp
    Game/IPlayableBoard.h
    Game/SandboxSession.h
    Game/SandboxSession.cpp
    Game/AnalysisBoardSession.h
    Game/AnalysisBoardSession.cpp
)

set(SETTINGS_SOURCES
    Settings/SettingsIni.h
    Settings/SettingsIni.cpp
)

# Library form of the app logic, so both ChessAssist and ChessAssistTests (see ../Tests)
# can link against it without recompiling everything twice.
add_library(ChessAssistCore STATIC ${ENGINE_SOURCES} ${CHESS_SOURCES} ${BROWSER_SOURCES} ${GAME_SOURCES} ${SETTINGS_SOURCES})

target_include_directories(ChessAssistCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${INI_CPP_INCLUDE_DIRS})
target_link_libraries(ChessAssistCore PUBLIC spdlog::spdlog Threads::Threads ixwebsocket::ixwebsocket nlohmann_json::nlohmann_json nfd::nfd)

set(UI_SOURCES
    UI/AppWindow.h
    UI/AppWindow.cpp
    UI/ControlsPanel.h
    UI/ControlsPanel.cpp
    UI/EngineInfoPanel.h
    UI/EngineInfoPanel.cpp
    UI/ChessPieceTextures.h
    UI/ChessPieceTextures.cpp
    UI/ChessBoardWidget.h
    UI/ChessBoardWidget.cpp
    UI/BoardStatePanel.h
    UI/BoardStatePanel.cpp
    UI/AnalysisBoardPanel.h
    UI/AnalysisBoardPanel.cpp
    UI/LogPanel.h
    UI/LogPanel.cpp
    UI/ImGuiLogSink.h
    UI/StbImageImpl.cpp
    UI/ImguiUtils.h
)

# UI sources are linked only into the ChessAssist executable, not ChessAssistCore - keeps
# GLFW/OpenGL out of ChessAssistTests, which has no window/GL context to run against.
add_executable(ChessAssist main.cpp App.h App.cpp ${UI_SOURCES})

target_include_directories(ChessAssist PRIVATE ${INI_CPP_INCLUDE_DIRS} ${Stb_INCLUDE_DIR} "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(ChessAssist PRIVATE ChessAssistCore imgui::imgui glfw OpenGL::GL)

add_dependencies(ChessAssist build_stockfish)

# GUI subsystem on Windows - the in-app Log panel already shows everything spdlog logs, so the
# separate console window Windows otherwise pops up alongside the GUI window is just noise.
# WIN32_EXECUTABLE alone switches the CRT startup stub to one that expects WinMain(), which
# main.cpp doesn't define (undefined symbol: WinMain) - the LINKER: option forces the entry
# point back to the normal main()-based startup, portably across both the MSVC and
# Clang+lld-link toolchains this project builds with.
if (WIN32)
    set_target_properties(ChessAssist PROPERTIES WIN32_EXECUTABLE ON)
    target_link_options(ChessAssist PRIVATE "LINKER:/ENTRY:mainCRTStartup")
endif()

# ExecutablePathUtil::GetAssetsDirectory() resolves to <exe-dir>/Assets at runtime (e.g. for
# AppWindow's bundled font) - keep that directory in sync with the source Assets/ tree on
# every build rather than requiring a separate manual copy step.
add_custom_command(TARGET ChessAssist POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Assets" "$<TARGET_FILE_DIR:ChessAssist>/Assets"
    COMMENT "Copying Assets to output directory..."
)
