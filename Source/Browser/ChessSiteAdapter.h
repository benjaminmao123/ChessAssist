#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

enum class ChessSite
{
    ChessDotCom,
    Lichess,
};

struct SiteGameState
{
    std::vector<std::string> SanMoves;  // full move list so far, move 1 first
    bool PlayingAsBlack = false;        // best-effort; UI-label use only, never load-bearing
};

// Reads a live game's move list out of the page DOM via CdpClient::EvaluateJs. Both sites
// use the same content-based extraction approach rather than a hardcoded container
// selector: lichess's live-round DOM uses tag names that are regenerated on every site
// deploy (confirmed against the lila source), so the script instead scans for whichever DOM
// subtree contains the most text nodes that look like SAN move tokens, and treats that as
// the move list - no selector to go stale. This is strictly more robust for chess.com too,
// even though its move list happens to sit in a stable <vertical-move-list> custom element.
namespace ChessSiteAdapter
{
std::string_view UrlMatchSubstring(ChessSite site);

// Same script for both sites today - kept as a per-site function since orientation
// detection (best-effort, see SiteGameState::PlayingAsBlack) may end up wanting a
// site-specific selector later.
std::string ExtractionScript(ChessSite site);

// jsonResult is the JSON text CdpClient::EvaluateJs returns for ExtractionScript's result -
// either a JSON object ({"moves": [...], "blackAtBottom": bool}) or the literal "null" if no
// game is currently open. Returns nullopt for "null" or any malformed/unparsable result.
std::optional<SiteGameState> ParseExtractionResult(std::string_view jsonResult);
}  // namespace ChessSiteAdapter
