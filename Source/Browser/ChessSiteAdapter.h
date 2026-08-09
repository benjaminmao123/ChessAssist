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

struct SquarePoint
{
    double X = 0.0;
    double Y = 0.0;
};

struct SquareCenters
{
    SquarePoint From;
    SquarePoint To;
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

// URL to navigate the app-managed browser to on launch, so the user lands straight on the
// site instead of a blank tab and having to type it in themselves.
std::string_view HomepageUrl(ChessSite site);

// Same script for both sites today - kept as a per-site function since orientation
// detection (best-effort, see SiteGameState::PlayingAsBlack) may end up wanting a
// site-specific selector later.
std::string ExtractionScript(ChessSite site);

// jsonResult is the JSON text CdpClient::EvaluateJs returns for ExtractionScript's result -
// either a JSON object ({"moves": [...], "blackAtBottom": bool}) or the literal "null" if no
// game is currently open. Returns nullopt for "null" or any malformed/unparsable result.
std::optional<SiteGameState> ParseExtractionResult(std::string_view jsonResult);

// Locates the site's board element (tries the known board custom elements/classes for both
// sites, content-based like ExtractionScript rather than a single hardcoded selector) and
// returns the viewport-relative CSS pixel centers of fromSquare and toSquare (each "e4"-style
// algebraic), accounting for board orientation. fromSquare/toSquare must already be validated
// algebraic squares (a-h/1-8) - they're spliced into the script unescaped.
std::string SquareCenterScript(std::string_view fromSquare, std::string_view toSquare, bool blackAtBottom);

// jsonResult is the JSON text CdpClient::EvaluateJs returns for SquareCenterScript's result -
// either {"from":{"x":..,"y":..},"to":{"x":..,"y":..}} or "null" if the board couldn't be
// found. Returns nullopt for "null" or any malformed/unparsable result.
std::optional<SquareCenters> ParseSquareCenters(std::string_view jsonResult);

// Best-effort: looks for a currently-visible promotion-piece picker option matching
// promotionLetter ('q', 'r', 'b', or 'n') and returns its viewport-relative CSS pixel center,
// or nullopt if none is found (e.g. the site hasn't opened a picker, or its DOM doesn't match
// the heuristics here) - callers should log and leave the promotion for the user to finish
// manually rather than guessing.
std::string PromotionPickerScript(char promotionLetter);
std::optional<SquarePoint> ParsePromotionTarget(std::string_view jsonResult);
}  // namespace ChessSiteAdapter
