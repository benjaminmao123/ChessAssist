#include "AnalysisBoardSession.h"

#include "Chess/ChessBoardOps.h"
#include "Chess/FenWriter.h"
#include "Engine/EngineController.h"

namespace
{
// Plain, fixed search time - same reasoning as SandboxSession's own kSandboxMoveTimeMs: just
// "analyze this position" quickly enough to feel responsive, no Elo/Blitz/book/premove concerns.
constexpr int kAnalysisMoveTimeMs = 1000;

MoveGenerator::PositionState StandardStartPosition()
{
    MoveGenerator::PositionState position;
    for (int rank = 0; rank < 8; ++rank)
        for (int file = 0; file < 8; ++file)
            position.Board[SquareIndex(file, rank)] = GetStandardStartingPiece(file, rank);

    position.SideToMove = PieceColor::White;
    return position;
}
}  // namespace

AnalysisBoardSession::AnalysisBoardSession(EngineController& engine)
    : m_Engine(&engine), m_StartPosition(StandardStartPosition()), m_Current(m_StartPosition)
{
    // Deliberately does NOT call RequestAnalysis() here - this constructor runs during App's
    // own construction, well before App::Run() actually starts m_Engine's process, so a search
    // requested this early would just hit EngineController::FindBestMoveAsync's "not running"
    // path and be silently discarded. App::Run() calls Reset() once, right after starting the
    // engine, to fire the first real request instead.
}

void AnalysisBoardSession::Reset()
{
    m_History.clear();
    m_Cursor = 0;
    RebuildCurrent();
    RequestAnalysis();
}

void AnalysisBoardSession::ResetToStandardStartingPosition()
{
    m_StartPosition = StandardStartPosition();
    Reset();
}

bool AnalysisBoardSession::LoadFen(std::string_view fen)
{
    const std::optional<MoveGenerator::PositionState> parsed = ParseFen(fen);
    if (!parsed)
        return false;

    m_StartPosition = *parsed;
    m_History.clear();
    m_Cursor = 0;
    RebuildCurrent();
    RequestAnalysis();
    return true;
}

std::string AnalysisBoardSession::GetFen() const
{
    return ToFen(m_Current.Board, m_Current.SideToMove, m_Current.Rights, m_Current.EnPassantTarget);
}

void AnalysisBoardSession::FlipBoard()
{
    m_Flipped = !m_Flipped;
}

bool AnalysisBoardSession::IsFlipped() const
{
    return m_Flipped;
}

void AnalysisBoardSession::StepBackward()
{
    if (!CanStepBackward())
        return;

    --m_Cursor;
    RebuildCurrent();
    RequestAnalysis();
}

void AnalysisBoardSession::StepForward()
{
    if (!CanStepForward())
        return;

    ++m_Cursor;
    RebuildCurrent();
    RequestAnalysis();
}

bool AnalysisBoardSession::CanStepBackward() const
{
    return m_Cursor > 0;
}

bool AnalysisBoardSession::CanStepForward() const
{
    return m_Cursor < m_History.size();
}

std::size_t AnalysisBoardSession::GetCursor() const
{
    return m_Cursor;
}

std::size_t AnalysisBoardSession::HistoryLength() const
{
    return m_History.size();
}

const BoardState& AnalysisBoardSession::GetBoard() const
{
    return m_Current.Board;
}

PieceColor AnalysisBoardSession::GetSideToMove() const
{
    return m_Current.SideToMove;
}

std::optional<int> AnalysisBoardSession::GetCheckedKingSquare() const
{
    const std::optional<int> kingSquare = ChessBoardOps::FindKing(m_Current.Board, m_Current.SideToMove);
    if (!kingSquare || !ChessBoardOps::IsSquareAttacked(m_Current.Board, *kingSquare, ChessBoardOps::Opposite(m_Current.SideToMove)))
        return std::nullopt;

    return kingSquare;
}

bool AnalysisBoardSession::IsBlackAtBottom() const
{
    return m_Flipped;
}

std::vector<MoveGenerator::LegalMove> AnalysisBoardSession::GetLegalMovesFrom(int from) const
{
    return MoveGenerator::GenerateLegalMovesFrom(m_Current, from);
}

void AnalysisBoardSession::PlayMove(const MoveGenerator::LegalMove& move)
{
    // A move played while the cursor is behind the end of history discards whatever "future"
    // was there - the standard PGN-viewer convention (see this method's header comment).
    if (m_Cursor < m_History.size())
        m_History.resize(m_Cursor);

    m_History.push_back(move);
    ++m_Cursor;
    RebuildCurrent();
    RequestAnalysis();
}

std::optional<std::string> AnalysisBoardSession::GetSuggestedMove() const
{
    std::scoped_lock lock(m_SuggestedMoveMutex);
    return m_SuggestedMove;
}

PieceColor AnalysisBoardSession::GetRequestedSide() const
{
    return m_RequestedSide.load();
}

void AnalysisBoardSession::OnEngineBestMove(const BestMoveResult& result)
{
    std::scoped_lock lock(m_SuggestedMoveMutex);
    m_SuggestedMove = result.BestMove;
}

void AnalysisBoardSession::RequestAnalysis()
{
    m_RequestedSide = m_Current.SideToMove;
    {
        std::scoped_lock lock(m_SuggestedMoveMutex);
        m_SuggestedMove.reset();
    }

    const std::string fen = ToFen(m_Current.Board, m_Current.SideToMove, m_Current.Rights, m_Current.EnPassantTarget);
    (void)m_Engine->FindBestMoveAsync(fen, SearchLimits{.MoveTimeMs = kAnalysisMoveTimeMs});
}

void AnalysisBoardSession::RebuildCurrent()
{
    m_Current = m_StartPosition;
    for (std::size_t i = 0; i < m_Cursor; ++i)
        MoveGenerator::ApplyMove(m_Current, m_History[i]);
}
