#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

struct CdpError
{
    std::string Message;
};

// Speaks the Chrome DevTools Protocol's JSON-RPC directly over a plain WebSocket (CDP on
// localhost is ws://, not wss://) - no CDP-specific client library exists for C++, and none
// is needed for the narrow slice used here: evaluate a JS expression in the page and get its
// result back. No event subscriptions - callers just re-run EvaluateJs on their own polling
// cadence, so unsolicited CDP events (without a matching request id) are simply ignored.
class CdpClient
{
public:
    CdpClient();
    ~CdpClient();
    CdpClient(const CdpClient&) = delete;
    CdpClient& operator=(const CdpClient&) = delete;

    // GET http://localhost:port/json, return the webSocketDebuggerUrl of the first open page
    // whose "url" field contains urlSubstring, or nullopt if none is open (yet).
    [[nodiscard]] static std::optional<std::string> FindPageWebSocketUrl(std::uint16_t port, std::string_view urlSubstring);

    [[nodiscard]] std::expected<void, CdpError> Connect(const std::string& webSocketDebuggerUrl, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));
    void Disconnect();
    [[nodiscard]] bool IsConnected() const;

    // Runs js via Runtime.evaluate(returnByValue=true) and blocks for the matching response.
    // Returns the JSON-serialized "result.value" on success; fails on a JS exception
    // (exceptionDetails present in the response), a malformed response, or timeout.
    [[nodiscard]] std::expected<std::string, CdpError> EvaluateJs(std::string_view js, std::chrono::milliseconds timeout = std::chrono::milliseconds(2000));

    // Simulates a left-button drag from (fromX, fromY) to (toX, toY) - viewport-relative CSS
    // pixel coordinates, e.g. from ChessSiteAdapter's board-square lookup - via CDP's Input
    // domain (mousePressed, a few interpolated mouseMoved steps, mouseReleased). Goes through
    // Chromium's real input pipeline rather than a page-script-dispatched DOM event, so the
    // resulting events are trusted (isTrusted: true) - some chess sites' anti-automation
    // checks reject the untrusted events a plain dispatchEvent() call would produce.
    [[nodiscard]] std::expected<void, CdpError> DragMouse(double fromX, double fromY, double toX, double toY, std::chrono::milliseconds timeout = std::chrono::milliseconds(2000));

    // Single trusted left-click at (x, y) - used for promotion-piece picker clicks.
    [[nodiscard]] std::expected<void, CdpError> Click(double x, double y, std::chrono::milliseconds timeout = std::chrono::milliseconds(2000));

private:
    // Sends one CDP command and blocks for its matching response - shared plumbing behind
    // EvaluateJs, DragMouse, and Click. jsonParams is the command's "params" object,
    // pre-serialized to JSON text by the caller (kept as text, not a parsed object, at this
    // boundary too - same reasoning as EvaluateJs's std::string return: keeps the JSON
    // library out of this header). Returns the response's "result" object as JSON text
    // ("{}" if absent).
    [[nodiscard]] std::expected<std::string, CdpError> SendCommand(std::string_view method, std::string_view jsonParams, std::chrono::milliseconds timeout);

    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
