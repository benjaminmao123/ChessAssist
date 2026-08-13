#pragma once

#include "ChessTypes.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Reads a Polyglot opening-book (.bin) file and looks up moves by Polyglot Zobrist hash - the
// standard format/hash scheme, so any book from a Polyglot-compatible tool works unmodified.
// See PolyglotBook.cpp for format/hash details.
class PolyglotBook
{
public:
    enum class SelectionMode
    {
        HighestWeight,   // deterministic: the (first) entry with the highest weight
        WeightedRandom,  // random draw, probability proportional to each entry's weight
    };

    // Reads and parses path fully into memory, replacing whatever was previously loaded.
    // Returns false (book left empty, as if Clear() had been called) on I/O or format failure;
    // callers should treat that as the feature being disabled, not an error.
    bool Load(const std::filesystem::path& path);

    void Clear();
    [[nodiscard]] bool IsLoaded() const;
    [[nodiscard]] std::size_t EntryCount() const;

    // Looks up the given position and returns a UCI move chosen per `mode`, or nullopt if the
    // book isn't loaded or has no entry (weight > 0) for this position. The chosen move is
    // validated against `board` before returning - defense against a hash collision or a book
    // that doesn't match the game - callers should fall back to normal search on nullopt.
    [[nodiscard]] std::optional<std::string> FindMove(const BoardState& board, PieceColor sideToMove, CastlingRights castling,
                                                        std::optional<int> enPassantTarget, SelectionMode mode) const;

    // Computes the same Polyglot Zobrist hash FindMove() uses internally - exposed so hash
    // correctness can be verified directly (see Tests/PolyglotBookTests.cpp).
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
