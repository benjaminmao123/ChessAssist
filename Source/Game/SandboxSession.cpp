#include "SandboxSession.h"

#include "Chess/ChessBoardOps.h"
#include "Chess/FenWriter.h"
#include "Engine/EngineController.h"

namespace
{
// Plain, fixed search time - no Elo/Blitz/book/premove concerns here, just "analyze this
// hypothetical position" quickly enough to feel responsive to dragging.
constexpr int kSandboxMoveTimeMs = 1000;
}  // namespace

SandboxSession::SandboxSession(EngineController& sandboxEngine)
    : m_Engine(&sandboxEngine)
{
}

void SandboxSession::SyncToLivePosition(const MoveGenerator::PositionState& livePosition, bool blackAtBottom)
{
    m_LiveSnapshot = livePosition;
    m_LiveBlackAtBottom = blackAtBottom;
    m_History.clear();
    RebuildCurrentAndRequery();
}

void SandboxSession::ResetToLive()
{
    m_History.clear();
    RebuildCurrentAndRequery();
}

bool SandboxSession::IsActive() const
{
    return !m_History.empty();
}

std::size_t SandboxSession::HistoryLength() const
{
    return m_History.size();
}

std::vector<MoveGenerator::LegalMove> SandboxSession::GetLegalMovesFrom(int from) const
{
    return MoveGenerator::GenerateLegalMovesFrom(m_Current, from);
}

void SandboxSession::PlayMove(const MoveGenerator::LegalMove& move)
{
    m_History.push_back(move);
    RebuildCurrentAndRequery();
}

void SandboxSession::UndoLastMove()
{
    if (m_History.empty())
        return;

    m_History.pop_back();
    RebuildCurrentAndRequery();
}

const BoardState& SandboxSession::GetBoard() const
{
    return m_Current.Board;
}

PieceColor SandboxSession::GetSideToMove() const
{
    return m_Current.SideToMove;
}

std::optional<int> SandboxSession::GetCheckedKingSquare() const
{
    const std::optional<int> kingSquare = ChessBoardOps::FindKing(m_Current.Board, m_Current.SideToMove);
    if (!kingSquare || !ChessBoardOps::IsSquareAttacked(m_Current.Board, *kingSquare, ChessBoardOps::Opposite(m_Current.SideToMove)))
        return std::nullopt;

    return kingSquare;
}

bool SandboxSession::IsBlackAtBottom() const
{
    return m_LiveBlackAtBottom;
}

std::optional<std::string> SandboxSession::GetSuggestedMove() const
{
    std::scoped_lock lock(m_SuggestedMoveMutex);
    return m_SuggestedMove;
}

PieceColor SandboxSession::GetRequestedSide() const
{
    return m_RequestedSide.load();
}

void SandboxSession::OnEngineBestMove(const BestMoveResult& result)
{
    std::scoped_lock lock(m_SuggestedMoveMutex);
    m_SuggestedMove = result.BestMove;
}

void SandboxSession::RequestSandboxSearch()
{
    m_RequestedSide = m_Current.SideToMove;
    {
        std::scoped_lock lock(m_SuggestedMoveMutex);
        m_SuggestedMove.reset();
    }

    const std::string fen = ToFen(m_Current.Board, m_Current.SideToMove, m_Current.Rights, m_Current.EnPassantTarget);
    (void)m_Engine->FindBestMoveAsync(fen, SearchLimits{.MoveTimeMs = kSandboxMoveTimeMs});
}

void SandboxSession::RebuildCurrentAndRequery()
{
    m_Current = m_LiveSnapshot;
    for (const MoveGenerator::LegalMove& move : m_History)
        MoveGenerator::ApplyMove(m_Current, move);

    if (m_History.empty())
    {
        m_Engine->StopSearch();
        std::scoped_lock lock(m_SuggestedMoveMutex);
        m_SuggestedMove.reset();
    }
    else
    {
        RequestSandboxSearch();
    }
}
