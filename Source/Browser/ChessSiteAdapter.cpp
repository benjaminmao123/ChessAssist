#include "ChessSiteAdapter.h"

#include <nlohmann/json.hpp>

namespace
{
// Content-based extraction: find the DOM subtree containing the most text nodes shaped like
// SAN move tokens, and read the move list from there - see ChessSiteAdapter.h for why this
// avoids depending on any specific container selector. That heuristic alone can't tell "zero
// moves played yet" apart from "no game open" - both look like zero SAN-shaped text anywhere
// - so it falls back to isGameOpen()'s board-presence check to distinguish them. Returns null
// only when neither finds anything.
constexpr const char* kExtractionScript = R"JS(
(() => {
    const sanPattern = /^(O-O-O|O-O|0-0-0|0-0|[KQRBN]?[a-h]?[1-8]?x?[a-h][1-8](=[QRBN])?[+#]?)$/;

    // chess.com renders a move's piece letter as an icon element (data-figurine="N" etc,
    // empty textContent) immediately preceding a plain-text node with just the destination
    // (e.g. "f3", "xd4") - the letter never reaches .textContent walking at all. Recover it
    // from the immediately preceding element sibling when present; harmless no-op on sites
    // (lichess included) that render the piece letter as ordinary text already.
    function figurineLetterFor(textNode) {
        let sibling = textNode.previousSibling;
        while (sibling) {
            if (sibling.nodeType === Node.ELEMENT_NODE) {
                const figurine = sibling.getAttribute && sibling.getAttribute('data-figurine');
                return figurine || null;
            }
            if (sibling.nodeType === Node.TEXT_NODE && sibling.textContent.trim()) return null;
            sibling = sibling.previousSibling;
        }
        return null;
    }

    function countSanTextNodes(root) {
        let count = 0;
        const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
        let node;
        while ((node = walker.nextNode())) {
            const text = node.textContent.trim();
            if (text && sanPattern.test(text)) count++;
        }
        return count;
    }

    function detectOrientation() {
        let blackAtBottom = false;
        try {
            if (document.querySelector('.cg-wrap.orientation-black')) blackAtBottom = true;
            else if (document.querySelector('.flipped')) blackAtBottom = true;
        } catch (e) {
            // Orientation is UI-only, never load-bearing - ignore failures here.
        }
        return blackAtBottom;
    }

    // Fallback for "is a game open at all" when the move-list heuristic below finds nothing -
    // which is exactly what happens at the very start of a fresh game (zero moves played
    // means zero SAN-shaped text anywhere yet, indistinguishable from no game being open at
    // all otherwise). The board itself, unlike the move list, renders as soon as a game is
    // open, before either side has moved. Deliberately only the sites' actual live-game
    // custom elements here, not a generic class selector like '.board' (used only for square
    // lookups elsewhere, where a false positive is far lower-stakes) - this result gates
    // whether polling treats the page as a live game at all, so a false positive here (e.g.
    // matching some unrelated promo widget on a site's homepage) risks requesting engine
    // moves, and with autoplay on, dragging on a page that isn't actually a live game.
    function isGameOpen() {
        return !!(document.querySelector('wc-chess-board') || document.querySelector('cg-board'));
    }

    let bestContainer = null;
    let bestCount = 0;

    const candidates = document.querySelectorAll('div, aside, section, vertical-move-list');
    for (const el of candidates) {
        if (el.children.length === 0) continue;
        const count = countSanTextNodes(el);
        if (count >= bestCount && count > 0) {
            bestCount = count;
            bestContainer = el;
        }
    }

    if (!bestContainer) return isGameOpen() ? { moves: [], blackAtBottom: detectOrientation() } : null;

    const moves = [];
    const walker = document.createTreeWalker(bestContainer, NodeFilter.SHOW_TEXT);
    let node;
    while ((node = walker.nextNode())) {
        const text = node.textContent.trim();
        if (!text) continue;

        const figurineLetter = figurineLetterFor(node);
        const candidateText = figurineLetter ? figurineLetter + text : text;
        if (sanPattern.test(candidateText)) moves.push(candidateText);
    }

    if (moves.length === 0) return isGameOpen() ? { moves: [], blackAtBottom: detectOrientation() } : null;

    return { moves, blackAtBottom: detectOrientation() };
})()
)JS";

// Same board-finding heuristic feeds both SquareCenterScript and, indirectly, orientation
// detection above: lichess's board layer is the <cg-board> custom element inside chessground's
// wrapper, chess.com's live board is the <wc-chess-board> custom element - both are exactly
// the 8x8 grid's own bounding box (no extra border/padding to subtract), and both are custom
// element tag names, which - per ExtractionScript's comment above on <vertical-move-list> -
// tend to survive site redesigns better than a styling class name would.
constexpr const char* kBoardElementScript = R"JS(
    let boardEl = null;
    for (const sel of ['wc-chess-board', 'cg-board', '.board']) {
        const el = document.querySelector(sel);
        if (!el) continue;
        const rect = el.getBoundingClientRect();
        if (rect.width > 50 && rect.height > 50) { boardEl = el; break; }
    }
)JS";

constexpr const char* kSquareCenterScriptTemplate = R"JS(
(() => {
%BOARD_ELEMENT_SCRIPT%
    if (!boardEl) return null;

    const rect = boardEl.getBoundingClientRect();
    const squareSize = rect.width / 8;
    const blackAtBottom = %BLACK_AT_BOTTOM%;

    function centerOf(square) {
        const file = square.charCodeAt(0) - 'a'.charCodeAt(0);
        const rank = square.charCodeAt(1) - '1'.charCodeAt(0);
        const col = blackAtBottom ? (7 - file) : file;
        const row = blackAtBottom ? rank : (7 - rank);
        return { x: rect.left + (col + 0.5) * squareSize, y: rect.top + (row + 0.5) * squareSize };
    }

    return { from: centerOf('%FROM_SQUARE%'), to: centerOf('%TO_SQUARE%') };
})()
)JS";

constexpr const char* kPromotionPickerScriptTemplate = R"JS(
(() => {
    const letterToName = { q: 'queen', r: 'rook', b: 'bishop', n: 'knight' };
    const wanted = letterToName['%PROMOTION_LETTER%'];
    if (!wanted) return null;

    // chess.com's live-game promotion picker: a '.promotion-window' (only actually open while
    // its '--visible' modifier class is present - it stays in the DOM, just off-screen,
    // the rest of the time) containing one '.promotion-piece' per option, each carrying a
    // second class named exactly '<color><type>' (e.g. "bq" = black queen) - matches this
    // app's own UCI color/promotion-letter convention directly, so it's tried first as an
    // exact match before falling back to the generic heuristic below (for lichess or any
    // other markup that doesn't use this structure).
    const exactWindow = document.querySelector('.promotion-window--visible') || document.querySelector('.promotion-window');
    if (exactWindow) {
        const exactPiece = exactWindow.querySelector('.promotion-piece.%PROMOTION_COLOR%%PROMOTION_LETTER%');
        if (exactPiece) {
            const rect = exactPiece.getBoundingClientRect();
            if (rect.width > 4 && rect.height > 4)
                return { x: rect.left + rect.width / 2, y: rect.top + rect.height / 2 };
        }
    }

    // lichess's promotion picker: a '#promotion-choice' element (present in the DOM only
    // while a promotion is pending) holding one custom <square> per option, each wrapping a
    // custom <piece class="<type> <color>"> - e.g. class="queen black" - full type/color
    // words rather than chess.com's abbreviated single-letter classes, and an id rather than
    // a class on the container, so it needs its own exact match: the generic heuristic below
    // only looks at data-piece/data-figurine/aria-label attributes and class names containing
    // "promotion"/"promote", none of which this markup has anywhere.
    const promotionChoice = document.getElementById('promotion-choice');
    if (promotionChoice) {
        const colorName = { w: 'white', b: 'black' }['%PROMOTION_COLOR%'];
        const exactPiece = promotionChoice.querySelector('piece.' + wanted + '.' + colorName);
        if (exactPiece) {
            const rect = exactPiece.getBoundingClientRect();
            if (rect.width > 4 && rect.height > 4)
                return { x: rect.left + rect.width / 2, y: rect.top + rect.height / 2 };
        }
    }

    const candidates = document.querySelectorAll('[data-piece], [data-figurine], [aria-label], [class*="promotion"] *, [class*="promote"] *');
    for (const el of candidates) {
        const hint = ((el.getAttribute('data-piece') || '') + ' ' + (el.getAttribute('aria-label') || '') + ' ' + (el.className || '')).toLowerCase();
        if (!hint.includes(wanted)) continue;

        const rect = el.getBoundingClientRect();
        if (rect.width > 4 && rect.height > 4)
            return { x: rect.left + rect.width / 2, y: rect.top + rect.height / 2 };
    }

    return null;
})()
)JS";

bool IsValidAlgebraicSquare(std::string_view square)
{
    return square.size() == 2 && square[0] >= 'a' && square[0] <= 'h' && square[1] >= '1' && square[1] <= '8';
}

std::string ReplaceAll(std::string text, std::string_view token, std::string_view replacement)
{
    std::size_t pos = 0;
    while ((pos = text.find(token, pos)) != std::string::npos)
    {
        text.replace(pos, token.size(), replacement);
        pos += replacement.size();
    }
    return text;
}

// Shared parse/catch boilerplate behind every ParseXxx() below - nullopt on any malformed/
// unparsable JSON text, same as each of them already documents for its own jsonResult
// parameter.
std::optional<nlohmann::json> TryParseJson(std::string_view jsonText)
{
    try
    {
        return nlohmann::json::parse(jsonText);
    }
    catch (const nlohmann::json::exception&)
    {
        return std::nullopt;
    }
}
}  // namespace

namespace ChessSiteAdapter
{
std::string_view UrlMatchSubstring(ChessSite site)
{
    switch (site)
    {
    case ChessSite::ChessDotCom:
        return "chess.com";
    case ChessSite::Lichess:
        return "lichess.org";
    }

    return "";
}

std::string_view HomepageUrl(ChessSite site)
{
    switch (site)
    {
    case ChessSite::ChessDotCom:
        return "https://www.chess.com/";
    case ChessSite::Lichess:
        return "https://lichess.org/";
    }

    return "";
}

std::string ExtractionScript(ChessSite)
{
    return kExtractionScript;
}

std::optional<SiteGameState> ParseExtractionResult(std::string_view jsonResult)
{
    const std::optional<nlohmann::json> parsed = TryParseJson(jsonResult);
    if (!parsed || parsed->is_null() || !parsed->is_object() || !parsed->contains("moves") || !(*parsed)["moves"].is_array())
        return std::nullopt;

    SiteGameState state;
    for (const nlohmann::json& move : (*parsed)["moves"])
    {
        if (!move.is_string())
            return std::nullopt;
        state.SanMoves.push_back(move.get<std::string>());
    }

    if (parsed->contains("blackAtBottom") && (*parsed)["blackAtBottom"].is_boolean())
        state.PlayingAsBlack = (*parsed)["blackAtBottom"].get<bool>();

    return state;
}

std::string SquareCenterScript(std::string_view fromSquare, std::string_view toSquare, bool blackAtBottom)
{
    if (!IsValidAlgebraicSquare(fromSquare) || !IsValidAlgebraicSquare(toSquare))
        return "null";

    std::string script = kSquareCenterScriptTemplate;
    script = ReplaceAll(std::move(script), "%BOARD_ELEMENT_SCRIPT%", kBoardElementScript);
    script = ReplaceAll(std::move(script), "%BLACK_AT_BOTTOM%", blackAtBottom ? "true" : "false");
    script = ReplaceAll(std::move(script), "%FROM_SQUARE%", fromSquare);
    script = ReplaceAll(std::move(script), "%TO_SQUARE%", toSquare);
    return script;
}

std::optional<SquareCenters> ParseSquareCenters(std::string_view jsonResult)
{
    const std::optional<nlohmann::json> parsed = TryParseJson(jsonResult);
    if (!parsed || !parsed->is_object() || !parsed->contains("from") || !parsed->contains("to"))
        return std::nullopt;

    const auto readPoint = [](const nlohmann::json& point) -> std::optional<SquarePoint> {
        if (!point.is_object() || !point.contains("x") || !point.contains("y") || !point["x"].is_number() || !point["y"].is_number())
            return std::nullopt;
        return SquarePoint{point["x"].get<double>(), point["y"].get<double>()};
    };

    const std::optional<SquarePoint> from = readPoint((*parsed)["from"]);
    const std::optional<SquarePoint> to = readPoint((*parsed)["to"]);
    if (!from || !to)
        return std::nullopt;

    return SquareCenters{*from, *to};
}

std::string PromotionPickerScript(char promotionLetter, char promotingColor)
{
    if (promotionLetter != 'q' && promotionLetter != 'r' && promotionLetter != 'b' && promotionLetter != 'n')
        return "null";
    if (promotingColor != 'w' && promotingColor != 'b')
        return "null";

    std::string script = kPromotionPickerScriptTemplate;
    script = ReplaceAll(std::move(script), "%PROMOTION_LETTER%", std::string_view(&promotionLetter, 1));
    script = ReplaceAll(std::move(script), "%PROMOTION_COLOR%", std::string_view(&promotingColor, 1));
    return script;
}

std::optional<SquarePoint> ParsePromotionTarget(std::string_view jsonResult)
{
    const std::optional<nlohmann::json> parsed = TryParseJson(jsonResult);
    if (!parsed || !parsed->is_object() || !parsed->contains("x") || !parsed->contains("y") || !(*parsed)["x"].is_number() || !(*parsed)["y"].is_number())
        return std::nullopt;

    return SquarePoint{(*parsed)["x"].get<double>(), (*parsed)["y"].get<double>()};
}
}  // namespace ChessSiteAdapter
