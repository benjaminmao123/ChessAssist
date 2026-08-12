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

std::vector<std::string> SandboxSession::GetAlternateMoves() const
{
    std::scoped_lock lock(m_AlternateMovesMutex);

    std::vector<std::string> moves;
    moves.reserve(m_AlternateMoves.size());
    for (const auto& [multiPvIndex, move] : m_AlternateMoves)
        moves.push_back(move);  // std::map iterates by key, so this is already index-ordered

    return moves;
}

std::optional<std::string> SandboxSession::GetLookaheadMove() const
{
    std::optional<LookaheadCandidate> candidate;
    {
        std::scoped_lock lock(m_LookaheadMutex);
        candidate = m_LookaheadCandidate;
    }
    if (!candidate)
        return std::nullopt;

    const std::optional<std::string> suggested = GetSuggestedMove();
    if (!suggested || candidate->OwnMove != *suggested)
        return std::nullopt;  // stale - the candidate no longer describes the current suggestion

    // Validate against a position with the suggestion actually applied, rather than trusting
    // the string and drawing it straight onto the still-one-ply-behind board - see
    // GameSession::GetLookaheadMove()'s comment for why (a pawn move, especially en passant,
    // can otherwise look outright illegal).
    MoveGenerator::PositionState position = m_Current;
    const std::optional<MoveGenerator::LegalMove> intermediate = MoveGenerator::FindLegalMove(position, candidate->OwnMove);
    if (!intermediate)
        return std::nullopt;

    MoveGenerator::ApplyMove(position, *intermediate);

    if (!MoveGenerator::FindLegalMove(position, candidate->ReplyMove))
        return std::nullopt;

    return candidate->ReplyMove;
}

void SandboxSession::OnEngineBestMove(const BestMoveResult& result)
{
    std::scoped_lock lock(m_SuggestedMoveMutex);
    m_SuggestedMove = result.BestMove;
}

void SandboxSession::OnEngineInfo(const SearchInfo& info)
{
    if (info.Pv.empty())
        return;

    if (info.MultiPvIndex >= 2)
    {
        std::scoped_lock lock(m_AlternateMovesMutex);
        m_AlternateMoves[info.MultiPvIndex] = info.Pv.front();
        return;
    }

    if (info.MultiPvIndex == 1 && info.Pv.size() >= 2)
    {
        std::scoped_lock lock(m_LookaheadMutex);
        m_LookaheadCandidate = LookaheadCandidate{info.Pv[0], info.Pv[1]};
    }
}

void SandboxSession::RequestSandboxSearch()
{
    m_RequestedSide = m_Current.SideToMove;
    {
        std::scoped_lock lock(m_SuggestedMoveMutex);
        m_SuggestedMove.reset();
    }
    {
        std::scoped_lock lock(m_AlternateMovesMutex);
        m_AlternateMoves.clear();
    }
    {
        std::scoped_lock lock(m_LookaheadMutex);
        m_LookaheadCandidate.reset();
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
        std::scoped_lock alternatesLock(m_AlternateMovesMutex);
        m_AlternateMoves.clear();
        std::scoped_lock lookaheadLock(m_LookaheadMutex);
        m_LookaheadCandidate.reset();
    }
    else
    {
        RequestSandboxSearch();
    }
}
