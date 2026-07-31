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

set(VISION_SOURCES
    Vision/VisionTypes.h
    Vision/VisionTypes.cpp
    Vision/ScreenCapture.h
    Vision/BoardCalibrator.h
    Vision/BoardCalibrator.cpp
    Vision/BoardSlicer.h
    Vision/BoardSlicer.cpp
    Vision/PieceTemplateLibrary.h
    Vision/PieceTemplateLibrary.cpp
    Vision/BoardStateExtractor.h
    Vision/BoardStateExtractor.cpp
)

if (WIN32)
    list(APPEND VISION_SOURCES Vision/ScreenCapture_Win32.cpp)
else()
    list(APPEND VISION_SOURCES Vision/ScreenCapture_Posix.cpp)
endif()

set(GAME_SOURCES
    Game/GameTracker.h
    Game/GameTracker.cpp
    Game/MoveDetector.h
    Game/MoveDetector.cpp
)

# Library form of the app logic, so both ChessAssist and ChessAssistTests (see ../Tests)
# can link against it without recompiling everything twice.
add_library(ChessAssistCore STATIC ${ENGINE_SOURCES} ${VISION_SOURCES} ${GAME_SOURCES})

target_include_directories(ChessAssistCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ChessAssistCore PUBLIC ${OpenCV_LIBS} spdlog::spdlog Threads::Threads)

if (WIN32)
    target_link_libraries(ChessAssistCore PUBLIC Gdi32 User32)
else()
    find_package(X11 REQUIRED)
    target_include_directories(ChessAssistCore PUBLIC ${X11_INCLUDE_DIR})
    target_link_libraries(ChessAssistCore PUBLIC ${X11_LIBRARIES})
endif()

add_executable(ChessAssist main.cpp)

target_include_directories(ChessAssist PRIVATE ${INI_CPP_INCLUDE_DIRS})
target_link_libraries(ChessAssist PRIVATE ChessAssistCore)

add_dependencies(ChessAssist build_stockfish)
