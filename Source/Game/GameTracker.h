#pragma once

#include "Chess/ChessTypes.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

class GameTracker
{
public:
    void Reset(std::string_view startingFen = kStandardStartFen);

    // Records a move (yours or the opponent's) that has been confirmed to have happened.
    void RecordMove(std::string_view uciMove);

    [[nodiscard]] const std::string& GetBaseFen() const;
    [[nodiscard]] std::span<const std::string> GetMoves() const;

    // Derived from the move count: base FEN is always a standard start (White to move
    // first), so an even move count means it's White's turn again.
    [[nodiscard]] PieceColor GetSideToMove() const;

    [[nodiscard]] const BoardState& GetLastKnownBoardState() const;
    void SetLastKnownBoardState(const BoardState& state);

private:
    std::string m_BaseFen{kStandardStartFen};
    std::vector<std::string> m_Moves;
    BoardState m_LastKnownBoardState{};
};
