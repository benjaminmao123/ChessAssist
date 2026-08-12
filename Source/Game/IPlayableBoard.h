#pragma once

#include "Chess/MoveGenerator.h"

#include <optional>
#include <vector>

// Minimal surface a drag-to-move-driven board needs - implemented by every session that backs
// an interactive chessboard (SandboxSession, AnalysisBoardSession), so the shared mouse-
// interaction/rendering code in Source/UI/ChessBoardWidget.h can operate on any of them without
// depending on a specific session type.
class IPlayableBoard
{
public:
    virtual ~IPlayableBoard() = default;

    [[nodiscard]] virtual const BoardState& GetBoard() const = 0;
    [[nodiscard]] virtual PieceColor GetSideToMove() const = 0;
    [[nodiscard]] virtual std::optional<int> GetCheckedKingSquare() const = 0;
    [[nodiscard]] virtual bool IsBlackAtBottom() const = 0;
    [[nodiscard]] virtual std::vector<MoveGenerator::LegalMove> GetLegalMovesFrom(int from) const = 0;
    virtual void PlayMove(const MoveGenerator::LegalMove& move) = 0;
};
