#include "GameTracker.h"

void GameTracker::Reset(std::string_view startingFen)
{
    m_BaseFen = std::string(startingFen);
    m_Moves.clear();
    m_LastKnownBoardState = BoardState{};
}

void GameTracker::RecordMove(std::string_view uciMove)
{
    m_Moves.emplace_back(uciMove);
}

const std::string& GameTracker::GetBaseFen() const
{
    return m_BaseFen;
}

std::span<const std::string> GameTracker::GetMoves() const
{
    return m_Moves;
}

PieceColor GameTracker::GetSideToMove() const
{
    return (m_Moves.size() % 2 == 0) ? PieceColor::White : PieceColor::Black;
}

const BoardState& GameTracker::GetLastKnownBoardState() const
{
    return m_LastKnownBoardState;
}

void GameTracker::SetLastKnownBoardState(const BoardState& state)
{
    m_LastKnownBoardState = state;
}
