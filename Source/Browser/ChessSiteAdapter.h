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

// Reads a live game's move list out of the page DOM via CdpClient::EvaluateJs. Both sites use
// content-based extraction (scan for the DOM subtree with the most SAN-shaped text nodes)
// rather than a hardcoded selector, since lichess regenerates its live-round tag names on
// every deploy; this is strictly more robust for chess.com too, despite chess.com's move list
// sitting in a stable <vertical-move-list> element.
namespace ChessSiteAdapter
{
std::string_view UrlMatchSubstring(ChessSite site);

std::string_view HomepageUrl(ChessSite site);

// Same script for both sites today - kept as a per-site function since orientation
// detection may later need a site-specific selector (see SiteGameState::PlayingAsBlack).
std::string ExtractionScript(ChessSite site);

// jsonResult is the JSON text CdpClient::EvaluateJs returns for ExtractionScript's result -
// either a JSON object ({"moves": [...], "blackAtBottom": bool}) or the literal "null" if no
// game is currently open. Returns nullopt for "null" or any malformed/unparsable result.
std::optional<SiteGameState> ParseExtractionResult(std::string_view jsonResult);

// Locates the site's board element (content-based, like ExtractionScript, rather than a
// hardcoded selector) and returns viewport-relative CSS pixel centers for fromSquare/toSquare
// ("e4"-style), accounting for orientation. fromSquare/toSquare must already be validated
// algebraic squares (a-h/1-8) - they're spliced into the script unescaped.
std::string SquareCenterScript(std::string_view fromSquare, std::string_view toSquare, bool blackAtBottom);

// jsonResult is the JSON text CdpClient::EvaluateJs returns for SquareCenterScript's result -
// either {"from":{"x":..,"y":..},"to":{"x":..,"y":..}} or "null" if the board couldn't be
// found. Returns nullopt for "null" or any malformed/unparsable result.
std::optional<SquareCenters> ParseSquareCenters(std::string_view jsonResult);

// Best-effort: finds the visible promotion-piece picker option matching promotionLetter
// ('q','r','b','n') for promotingColor ('w'/'b' - always the tracked player's own color) and
// returns its viewport-relative CSS pixel center, or nullopt if no picker/match is found -
// callers should log and leave the promotion for the user to finish manually rather than guess.
std::string PromotionPickerScript(char promotionLetter, char promotingColor);
std::optional<SquarePoint> ParsePromotionTarget(std::string_view jsonResult);
}  // namespace ChessSiteAdapter
