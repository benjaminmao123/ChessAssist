#include "ChessSiteAdapter.h"

#include <nlohmann/json.hpp>

namespace
{
// Content-based extraction: find the DOM subtree containing the most text nodes shaped like
// SAN move tokens, and read the move list from there - see ChessSiteAdapter.h for why this
// avoids depending on any specific container selector. Returns null if no game is open
// (nothing on the page has enough SAN-shaped text to look like a move list).
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

    if (!bestContainer) return null;

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

    if (moves.length === 0) return null;

    let blackAtBottom = false;
    try {
        if (document.querySelector('.cg-wrap.orientation-black')) blackAtBottom = true;
        else if (document.querySelector('.flipped')) blackAtBottom = true;
    } catch (e) {
        // Orientation is UI-only, never load-bearing - ignore failures here.
    }

    return { moves, blackAtBottom };
})()
)JS";
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

std::string ExtractionScript(ChessSite)
{
    return kExtractionScript;
}

std::optional<SiteGameState> ParseExtractionResult(std::string_view jsonResult)
{
    nlohmann::json parsed;
    try
    {
        parsed = nlohmann::json::parse(jsonResult);
    }
    catch (const nlohmann::json::exception&)
    {
        return std::nullopt;
    }

    if (parsed.is_null() || !parsed.is_object() || !parsed.contains("moves") || !parsed["moves"].is_array())
        return std::nullopt;

    SiteGameState state;
    for (const nlohmann::json& move : parsed["moves"])
    {
        if (!move.is_string())
            return std::nullopt;
        state.SanMoves.push_back(move.get<std::string>());
    }

    if (parsed.contains("blackAtBottom") && parsed["blackAtBottom"].is_boolean())
        state.PlayingAsBlack = parsed["blackAtBottom"].get<bool>();

    return state;
}
}  // namespace ChessSiteAdapter
