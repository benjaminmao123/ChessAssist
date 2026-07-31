#include "GameSession.h"

#include "MoveDetector.h"

#include "../Engine/ExecutablePathUtil.h"
#include "../Vision/BoardSlicer.h"
#include "../Vision/BoardStateExtractor.h"

#include <spdlog/spdlog.h>

#include <opencv2/imgcodecs.hpp>

#include <array>
#include <filesystem>
#include <vector>

namespace
{
// GameSession always receives frames already cropped to the board region (via
// ScreenCapture::CaptureRegion(m_Region.Rect)), so the frame's own (0,0) is the board's
// top-left on screen - the *absolute* region rect must never be handed to SliceCells
// against such a frame, or it double-crops against the wrong coordinate space and slices
// garbage cells. Only orientation from m_Region still applies.
BoardRegion LocalRegion(const cv::Mat& frame, const BoardRegion& region)
{
    return BoardRegion{cv::Rect(0, 0, frame.cols, frame.rows), region.Orientation};
}

std::string DescribePiece(const std::optional<Piece>& piece)
{
    if (!piece)
        return "empty";

    static constexpr const char* kTypeNames[] = { "pawn", "knight", "bishop", "rook", "queen", "king" };
    return std::string(piece->Color == PieceColor::White ? "white " : "black ") + kTypeNames[static_cast<int>(piece->Type)];
}

// Temporary diagnostic aid: dumps the frame and the individual cell crops for every
// changed square next to the running executable (fixed filenames, overwritten each poll)
// so a misdetection can be inspected after the fact without needing to reproduce it live.
void SaveDebugCapture(const cv::Mat& frame, const std::array<cv::Mat, 64>& cells, const std::vector<int>& changedSquares)
{
    const std::filesystem::path debugDir = ExecutablePathUtil::GetCurrentExecutablePath().parent_path() / "DebugCaptures";
    std::error_code             ec;
    std::filesystem::create_directories(debugDir, ec);

    cv::imwrite((debugDir / "frame.png").string(), frame);

    for (int index : changedSquares)
        cv::imwrite((debugDir / (SquareToAlgebraic(index) + ".png")).string(), cells[index]);
}
}  // namespace

GameSession::GameSession(EngineController& controller)
    : m_Controller(&controller)
{
}

bool GameSession::LoadPieceTemplates(const std::filesystem::path& assetsDirectory)
{
    return m_TemplateLibrary.BootstrapFromReferenceAssets(assetsDirectory);
}

bool GameSession::AreTemplatesLoaded() const
{
    return m_TemplateLibrary.IsBootstrapped();
}

bool GameSession::StartNewGame(const cv::Mat& frame, const BoardRegion& region)
{
    if (!m_TemplateLibrary.IsBootstrapped())
    {
        m_Active = false;
        return false;
    }

    m_Region = region;

    const std::array<cv::Mat, 64> cells = BoardSlicer::SliceCells(frame, LocalRegion(frame, m_Region));

    m_Tracker.Reset();
    m_Tracker.SetLastKnownBoardState(BoardStateExtractor::Extract(cells, m_TemplateLibrary));

    m_Active = true;
    RequestEngineMove();

    return true;
}

bool GameSession::IsActive() const
{
    return m_Active;
}

const BoardRegion& GameSession::GetRegion() const
{
    return m_Region;
}

const GameTracker& GameSession::GetTracker() const
{
    return m_Tracker;
}

std::optional<std::string> GameSession::Poll(const cv::Mat& frame)
{
    if (!m_Active)
        return std::nullopt;

    const std::array<cv::Mat, 64> cells        = BoardSlicer::SliceCells(frame, LocalRegion(frame, m_Region));
    const BoardState              currentState = BoardStateExtractor::Extract(cells, m_TemplateLibrary);

    const BoardState& lastState = m_Tracker.GetLastKnownBoardState();

    std::vector<int> changedSquares;
    for (int i = 0; i < 64; ++i)
    {
        if (lastState[i] != currentState[i])
            changedSquares.push_back(i);
    }

    const std::optional<std::string> move = MoveDetector::DetectMove(lastState, currentState, m_Tracker.GetSideToMove());

    if (!move)
    {
        // Diagnostic only: a real move should show up as exactly a 2/3/4-square change that
        // MoveDetector recognizes. Logging every other case (0 changes when one was expected,
        // or an unrecognized change shape) makes it possible to tell live-capture recognition
        // problems apart from move-detection logic problems without needing to reproduce it.
        if (!changedSquares.empty())
        {
            spdlog::warn("Poll: {} square(s) changed but no move recognized (side to move: {})", changedSquares.size(), m_Tracker.GetSideToMove() == PieceColor::White ? "White" : "Black");

            for (int index : changedSquares)
                spdlog::warn("  {}: {} -> {}", SquareToAlgebraic(index), DescribePiece(lastState[index]), DescribePiece(currentState[index]));

            SaveDebugCapture(frame, cells, changedSquares);
            spdlog::warn("Saved debug capture to DebugCaptures/ next to the executable");
        }

        return std::nullopt;
    }

    m_Tracker.RecordMove(*move);
    m_Tracker.SetLastKnownBoardState(currentState);

    RequestEngineMove();

    return move;
}

void GameSession::RequestEngineMove()
{
    SearchLimits limits;
    limits.MoveTimeMs = 1500;
    (void)m_Controller->FindBestMoveAsync(m_Tracker.GetBaseFen(), limits, m_Tracker.GetMoves());
}
