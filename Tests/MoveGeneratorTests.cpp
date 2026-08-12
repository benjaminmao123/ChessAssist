#include "Chess/MoveGenerator.h"

#include "Chess/ChessBoardOps.h"
#include "Chess/ChessRules.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace
{
using LegalMove = MoveGenerator::LegalMove;
using PositionState = MoveGenerator::PositionState;

PositionState FromRules(const ChessRules& rules)
{
    return PositionState{rules.GetBoard(), rules.GetSideToMove(), rules.GetCastlingRights(), rules.GetEnPassantTarget()};
}

PositionState EmptyPosition(PieceColor sideToMove)
{
    PositionState position;
    position.SideToMove = sideToMove;
    position.Rights = CastlingRights{false, false, false, false};
    return position;
}

bool Contains(const std::vector<LegalMove>& moves, int from, int to, std::optional<PieceType> promotion = std::nullopt)
{
    return std::any_of(moves.begin(), moves.end(), [&](const LegalMove& move)
                        { return move.From == from && move.To == to && move.Promotion == promotion; });
}
}  // namespace

TEST(MoveGeneratorTest, StartingPositionHasTwentyLegalMoves)
{
    ChessRules rules;
    rules.Reset();

    EXPECT_EQ(MoveGenerator::GenerateLegalMoves(FromRules(rules)).size(), 20u);
}

TEST(MoveGeneratorTest, PinnedKnightHasNoLegalMoves)
{
    ChessRules rules;
    rules.Reset();

    ASSERT_EQ(rules.ApplySanMove("d4"), "d2d4");
    ASSERT_EQ(rules.ApplySanMove("e5"), "e7e5");
    ASSERT_EQ(rules.ApplySanMove("Nc3"), "b1c3");
    ASSERT_EQ(rules.ApplySanMove("Bb4"), "f8b4");  // pins the c3 knight to White's king on e1

    const PositionState position = FromRules(rules);
    const std::vector<LegalMove> knightMoves = MoveGenerator::GenerateLegalMovesFrom(position, SquareIndex(2, 2));  // c3
    EXPECT_TRUE(knightMoves.empty());
}

TEST(MoveGeneratorTest, EnPassantCaptureIsGenerated)
{
    ChessRules rules;
    rules.Reset();

    ASSERT_EQ(rules.ApplySanMove("e4"), "e2e4");
    ASSERT_EQ(rules.ApplySanMove("a6"), "a7a6");
    ASSERT_EQ(rules.ApplySanMove("e5"), "e4e5");
    ASSERT_EQ(rules.ApplySanMove("d5"), "d7d5");

    const PositionState position = FromRules(rules);
    const std::vector<LegalMove> pawnMoves = MoveGenerator::GenerateLegalMovesFrom(position, SquareIndex(4, 4));  // e5

    const auto it = std::find_if(pawnMoves.begin(), pawnMoves.end(), [](const LegalMove& move) { return move.IsEnPassant; });
    ASSERT_NE(it, pawnMoves.end());
    EXPECT_EQ(it->To, SquareIndex(3, 5));  // d6
}

TEST(MoveGeneratorTest, PromotionExpandsToFourMoves)
{
    ChessRules rules;
    rules.Reset();

    ASSERT_EQ(rules.ApplySanMove("h4"), "h2h4");
    ASSERT_EQ(rules.ApplySanMove("g5"), "g7g5");
    ASSERT_EQ(rules.ApplySanMove("hxg5"), "h4g5");
    ASSERT_EQ(rules.ApplySanMove("a6"), "a7a6");
    ASSERT_EQ(rules.ApplySanMove("g6"), "g5g6");
    ASSERT_EQ(rules.ApplySanMove("a5"), "a6a5");
    ASSERT_EQ(rules.ApplySanMove("gxh7"), "g6h7");
    ASSERT_EQ(rules.ApplySanMove("a4"), "a5a4");

    const PositionState position = FromRules(rules);
    const std::vector<LegalMove> pawnMoves = MoveGenerator::GenerateLegalMovesFrom(position, SquareIndex(7, 6));  // h7

    for (PieceType promo : {PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight})
        EXPECT_TRUE(Contains(pawnMoves, SquareIndex(7, 6), SquareIndex(6, 7), promo)) << "missing promotion to " << static_cast<int>(promo);
}

TEST(MoveGeneratorTest, CastlingRequiresRights)
{
    PositionState position = EmptyPosition(PieceColor::White);
    position.Board[SquareIndex(4, 0)] = Piece{PieceType::King, PieceColor::White};
    position.Board[SquareIndex(7, 0)] = Piece{PieceType::Rook, PieceColor::White};
    // WhiteKingside deliberately left false (EmptyPosition starts every right false).

    const std::vector<LegalMove> kingMoves = MoveGenerator::GenerateLegalMovesFrom(position, SquareIndex(4, 0));
    EXPECT_FALSE(Contains(kingMoves, SquareIndex(4, 0), SquareIndex(6, 0)));
}

TEST(MoveGeneratorTest, CastlingBlockedByOccupancy)
{
    PositionState position = EmptyPosition(PieceColor::White);
    position.Rights.WhiteKingside = true;
    position.Board[SquareIndex(4, 0)] = Piece{PieceType::King, PieceColor::White};
    position.Board[SquareIndex(7, 0)] = Piece{PieceType::Rook, PieceColor::White};
    position.Board[SquareIndex(5, 0)] = Piece{PieceType::Bishop, PieceColor::White};  // f1 occupied

    const std::vector<LegalMove> kingMoves = MoveGenerator::GenerateLegalMovesFrom(position, SquareIndex(4, 0));
    EXPECT_FALSE(Contains(kingMoves, SquareIndex(4, 0), SquareIndex(6, 0)));
}

TEST(MoveGeneratorTest, CastlingBlockedWhenKingInCheck)
{
    PositionState position = EmptyPosition(PieceColor::White);
    position.Rights.WhiteKingside = true;
    position.Board[SquareIndex(4, 0)] = Piece{PieceType::King, PieceColor::White};
    position.Board[SquareIndex(7, 0)] = Piece{PieceType::Rook, PieceColor::White};
    position.Board[SquareIndex(4, 7)] = Piece{PieceType::Rook, PieceColor::Black};  // checks e1 along the e-file

    const std::vector<LegalMove> kingMoves = MoveGenerator::GenerateLegalMovesFrom(position, SquareIndex(4, 0));
    EXPECT_FALSE(Contains(kingMoves, SquareIndex(4, 0), SquareIndex(6, 0)));
}

TEST(MoveGeneratorTest, CastlingBlockedWhenPassingThroughAttackedSquare)
{
    PositionState position = EmptyPosition(PieceColor::White);
    position.Rights.WhiteKingside = true;
    position.Board[SquareIndex(4, 0)] = Piece{PieceType::King, PieceColor::White};
    position.Board[SquareIndex(7, 0)] = Piece{PieceType::Rook, PieceColor::White};
    position.Board[SquareIndex(5, 7)] = Piece{PieceType::Rook, PieceColor::Black};  // attacks f1, the transit square

    const std::vector<LegalMove> kingMoves = MoveGenerator::GenerateLegalMovesFrom(position, SquareIndex(4, 0));
    EXPECT_FALSE(Contains(kingMoves, SquareIndex(4, 0), SquareIndex(6, 0)));
}

TEST(MoveGeneratorTest, CastlingBlockedWhenDestinationAttacked)
{
    PositionState position = EmptyPosition(PieceColor::White);
    position.Rights.WhiteKingside = true;
    position.Board[SquareIndex(4, 0)] = Piece{PieceType::King, PieceColor::White};
    position.Board[SquareIndex(7, 0)] = Piece{PieceType::Rook, PieceColor::White};
    position.Board[SquareIndex(6, 7)] = Piece{PieceType::Rook, PieceColor::Black};  // attacks g1, the destination

    const std::vector<LegalMove> kingMoves = MoveGenerator::GenerateLegalMovesFrom(position, SquareIndex(4, 0));
    EXPECT_FALSE(Contains(kingMoves, SquareIndex(4, 0), SquareIndex(6, 0)));
}

TEST(MoveGeneratorTest, CastlingSucceedsWhenLegal)
{
    ChessRules rules;
    rules.Reset();

    ASSERT_EQ(rules.ApplySanMove("e4"), "e2e4");
    ASSERT_EQ(rules.ApplySanMove("e5"), "e7e5");
    ASSERT_EQ(rules.ApplySanMove("Nf3"), "g1f3");
    ASSERT_EQ(rules.ApplySanMove("Nc6"), "b8c6");
    ASSERT_EQ(rules.ApplySanMove("Bc4"), "f1c4");
    ASSERT_EQ(rules.ApplySanMove("Bc5"), "f8c5");

    const PositionState position = FromRules(rules);
    const std::vector<LegalMove> kingMoves = MoveGenerator::GenerateLegalMovesFrom(position, SquareIndex(4, 0));

    const auto it = std::find_if(kingMoves.begin(), kingMoves.end(), [](const LegalMove& move) { return move.IsCastle; });
    ASSERT_NE(it, kingMoves.end());
    EXPECT_EQ(it->To, SquareIndex(6, 0));

    PositionState after = position;
    MoveGenerator::ApplyMove(after, *it);
    EXPECT_EQ(after.Board[SquareIndex(5, 0)], (Piece{PieceType::Rook, PieceColor::White}));  // rook landed on f1
    EXPECT_FALSE(after.Board[SquareIndex(7, 0)].has_value());                                // vacated h1
}

TEST(MoveGeneratorTest, DetectsCheckmate)
{
    ChessRules rules;
    rules.Reset();

    ASSERT_TRUE(rules.ApplySanMove("f3").has_value());
    ASSERT_TRUE(rules.ApplySanMove("e5").has_value());
    ASSERT_TRUE(rules.ApplySanMove("g4").has_value());
    ASSERT_TRUE(rules.ApplySanMove("Qh4#").has_value());

    const PositionState position = FromRules(rules);
    EXPECT_TRUE(MoveGenerator::HasNoLegalMoves(position));
}

TEST(MoveGeneratorTest, DetectsStalemate)
{
    // Textbook stalemate: White king h1, Black king f2, Black queen g3 - White to move, not
    // in check, but g1/g2/h2 are all covered (g1 by the Black king, g2/h2 by the queen).
    PositionState position = EmptyPosition(PieceColor::White);
    position.Board[SquareIndex(7, 0)] = Piece{PieceType::King, PieceColor::White};
    position.Board[SquareIndex(5, 1)] = Piece{PieceType::King, PieceColor::Black};
    position.Board[SquareIndex(6, 2)] = Piece{PieceType::Queen, PieceColor::Black};

    EXPECT_TRUE(MoveGenerator::HasNoLegalMoves(position));

    // Cross-check "not in check" independently via ChessBoardOps' own attack detection so this
    // test doesn't just trust MoveGenerator's own filtering to prove the position is a
    // stalemate rather than checkmate.
    EXPECT_FALSE(ChessBoardOps::IsSquareAttacked(position.Board, SquareIndex(7, 0), PieceColor::Black));
}

TEST(MoveGeneratorTest, ApplyMoveSetsEnPassantTargetOnDoublePush)
{
    ChessRules rules;
    rules.Reset();

    PositionState position = FromRules(rules);
    const std::vector<LegalMove> pawnMoves = MoveGenerator::GenerateLegalMovesFrom(position, SquareIndex(4, 1));  // e2
    const auto it = std::find_if(pawnMoves.begin(), pawnMoves.end(), [](const LegalMove& move) { return move.To == SquareIndex(4, 3); });
    ASSERT_NE(it, pawnMoves.end());

    MoveGenerator::ApplyMove(position, *it);
    ASSERT_TRUE(position.EnPassantTarget.has_value());
    EXPECT_EQ(*position.EnPassantTarget, SquareIndex(4, 2));  // e3
    EXPECT_EQ(position.SideToMove, PieceColor::Black);
}

TEST(MoveGeneratorTest, EnPassantNotOfferedWithoutARecentDoublePush)
{
    // Black's d-pawn walked to d5 one square at a time over two separate moves (not a double
    // push) - White's adjacent e5 pawn must not be offered an en passant capture to d6.
    ChessRules rules;
    rules.Reset();

    ASSERT_EQ(rules.ApplySanMove("e4"), "e2e4");
    ASSERT_EQ(rules.ApplySanMove("d6"), "d7d6");
    ASSERT_EQ(rules.ApplySanMove("e5"), "e4e5");
    ASSERT_EQ(rules.ApplySanMove("d5"), "d6d5");  // single push onto d5, not a double push

    const PositionState position = FromRules(rules);
    EXPECT_FALSE(position.EnPassantTarget.has_value());

    const std::vector<LegalMove> pawnMoves = MoveGenerator::GenerateLegalMovesFrom(position, SquareIndex(4, 4));  // e5
    EXPECT_FALSE(std::any_of(pawnMoves.begin(), pawnMoves.end(), [](const LegalMove& move) { return move.IsEnPassant; }));
    EXPECT_FALSE(Contains(pawnMoves, SquareIndex(4, 4), SquareIndex(3, 5)));  // e5 to d6
}

TEST(MoveGeneratorTest, EnPassantRightExpiresAfterAnIntermediateMove)
{
    // Black double-pushes to d5, creating a real en passant opportunity for White's e5 pawn -
    // but White plays something else first. The opportunity must not still be available
    // afterward, even though the pawns themselves haven't moved.
    ChessRules rules;
    rules.Reset();

    ASSERT_EQ(rules.ApplySanMove("e4"), "e2e4");
    ASSERT_EQ(rules.ApplySanMove("a6"), "a7a6");
    ASSERT_EQ(rules.ApplySanMove("e5"), "e4e5");
    ASSERT_EQ(rules.ApplySanMove("d5"), "d7d5");  // en passant to d6 is available right now

    ASSERT_EQ(rules.ApplySanMove("Nf3"), "g1f3");  // White declines it, plays something else
    ASSERT_EQ(rules.ApplySanMove("a5"), "a6a5");

    const PositionState position = FromRules(rules);
    EXPECT_FALSE(position.EnPassantTarget.has_value());

    const std::vector<LegalMove> pawnMoves = MoveGenerator::GenerateLegalMovesFrom(position, SquareIndex(4, 4));  // e5
    EXPECT_FALSE(std::any_of(pawnMoves.begin(), pawnMoves.end(), [](const LegalMove& move) { return move.IsEnPassant; }));
}

TEST(MoveGeneratorTest, EnPassantCaptureIllegalWhenItExposesOwnKingToDiscoveredCheck)
{
    // Classic tricky en passant edge case: King a5, White pawn e5, Black pawn d5 (just double-
    // pushed from d7), Black rook h5 - capturing e5xd6 e.p. removes BOTH the e5 pawn (moves
    // away) and the d5 pawn (captured) from rank 5 in one move, opening the entire a5-h5 rank
    // between the king and the rook. Purely a rank-file reachability check (CanPieceReach/
    // IsSquareAttacked) would miss this since neither pawn "pins" the other individually - only
    // simulating the actual resulting board (see LeavesOwnKingSafe) catches it.
    PositionState position = EmptyPosition(PieceColor::White);
    position.Board[SquareIndex(0, 4)] = Piece{PieceType::King, PieceColor::White};   // a5
    position.Board[SquareIndex(4, 4)] = Piece{PieceType::Pawn, PieceColor::White};   // e5
    position.Board[SquareIndex(3, 4)] = Piece{PieceType::Pawn, PieceColor::Black};   // d5 (just double-pushed)
    position.Board[SquareIndex(7, 4)] = Piece{PieceType::Rook, PieceColor::Black};   // h5
    position.Board[SquareIndex(0, 7)] = Piece{PieceType::King, PieceColor::Black};   // a8 - every position needs a black king
    position.EnPassantTarget = SquareIndex(3, 5);                                    // d6

    const std::vector<LegalMove> pawnMoves = MoveGenerator::GenerateLegalMovesFrom(position, SquareIndex(4, 4));  // e5
    EXPECT_FALSE(std::any_of(pawnMoves.begin(), pawnMoves.end(), [](const LegalMove& move) { return move.IsEnPassant; }))
        << "en passant capture should be illegal - it discovers a rook check along rank 5";
}

TEST(MoveGeneratorTest, ToUciFormatsPromotion)
{
    const LegalMove move{SquareIndex(6, 6), SquareIndex(6, 7), PieceType::Knight, false, false};
    EXPECT_EQ(MoveGenerator::ToUci(move), "g7g8n");
}

TEST(MoveGeneratorTest, FindLegalMoveResolvesUciStringToTheMatchingMove)
{
    ChessRules rules;
    rules.Reset();

    const PositionState position = FromRules(rules);
    const std::optional<LegalMove> move = MoveGenerator::FindLegalMove(position, "e2e4");

    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(move->From, SquareIndex(4, 1));
    EXPECT_EQ(move->To, SquareIndex(4, 3));
}

TEST(MoveGeneratorTest, FindLegalMoveReturnsNulloptForAnIllegalString)
{
    ChessRules rules;
    rules.Reset();

    const PositionState position = FromRules(rules);
    EXPECT_FALSE(MoveGenerator::FindLegalMove(position, "e2e5").has_value());  // not a legal starting move
}
