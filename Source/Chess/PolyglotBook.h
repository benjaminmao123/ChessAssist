#pragma once

#include "ChessTypes.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Reads a Polyglot opening-book (.bin) file and looks up moves for a given position by its
// Polyglot Zobrist hash - the same binary format and hash scheme every Polyglot-compatible
// tool (the original PolyGlot, Arena, CuteChess, ...) uses, so any book downloaded for one of
// those works here unmodified. See PolyglotBook.cpp for format/hash details.
class PolyglotBook
{
public:
    enum class SelectionMode
    {
        HighestWeight,   // deterministic: the (first) entry with the highest weight
        WeightedRandom,  // random draw, probability proportional to each entry's weight
    };

    // Reads and parses path fully into memory, replacing whatever was previously loaded.
    // Returns false (leaving the book empty, as if Clear() had been called) on any I/O or
    // format failure - logs the reason. Callers should treat that the same as the feature
    // being disabled rather than erroring out.
    bool Load(const std::filesystem::path& path);

    void Clear();
    [[nodiscard]] bool IsLoaded() const;
    [[nodiscard]] std::size_t EntryCount() const;

    // Looks up the given position - fully described by board/sideToMove/castling/
    // enPassantTarget, matching exactly what ChessRules::GetBoard()/GetSideToMove()/
    // GetCastlingRights()/GetEnPassantTarget() already expose - and returns a UCI move chosen
    // per `mode`, or nullopt if the book isn't loaded or has no entry (with weight > 0) for
    // this exact position. The chosen move is validated against `board` before being returned
    // (a piece of `sideToMove`'s color must actually occupy the decoded from-square) as a
    // defense against a hash collision or a book file that doesn't match the game being
    // played - callers should fall back to normal engine search on nullopt, same as "not in
    // book".
    [[nodiscard]] std::optional<std::string> FindMove(const BoardState& board, PieceColor sideToMove, CastlingRights castling,
                                                        std::optional<int> enPassantTarget, SelectionMode mode) const;

    // Computes the same Polyglot Zobrist hash FindMove() looks up internally - exposed so
    // hash correctness can be verified directly (see Tests/PolyglotBookTests.cpp) independent
    // of any .bin file.
    [[nodiscard]] static std::uint64_t ComputeHash(const BoardState& board, PieceColor sideToMove, CastlingRights castling,
                                                     std::optional<int> enPassantTarget);

private:
    struct Entry
    {
        std::uint64_t Key = 0;
        std::uint16_t Move = 0;
        std::uint16_t Weight = 0;
    };

    std::vector<Entry> m_Entries;  // ascending by Key - the file's own guaranteed order, not re-sorted
};
