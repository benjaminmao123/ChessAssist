#pragma once

#include "GameTracker.h"

#include "../Browser/BrowserLauncher.h"
#include "../Browser/CdpClient.h"
#include "../Browser/ChessSiteAdapter.h"
#include "../Chess/ChessRules.h"

#include <filesystem>
#include <string>
#include <vector>

class EngineController;

// Orchestrates a live game: launches and owns a dedicated Chrome instance (BrowserLauncher),
// polls the chess site's move-list DOM through it (CdpClient + ChessSiteAdapter), converts
// newly-seen SAN moves to UCI (ChessRules), and feeds them into the unchanged
// GameTracker/EngineController pipeline.
class GameSession
{
public:
    explicit GameSession(EngineController& controller);

    // One-time per app session: launches (or no-ops if already running) the app-managed
    // Chrome instance with CDP remote debugging enabled.
    [[nodiscard]] std::expected<void, BrowserError> LaunchBrowser(const std::filesystem::path& profileDir);
    [[nodiscard]] bool IsBrowserRunning() const;

    // Finds site's already-open tab in the app-managed Chrome, connects, and resets
    // tracking - the next Poll() baselines from whatever moves already exist in the site's
    // move list (0 for a fresh game, N for joining mid-game, handled automatically by Poll's
    // diff logic - no special-casing needed here).
    [[nodiscard]] bool ConnectToSite(ChessSite site);
    [[nodiscard]] bool IsConnected() const;

    // Ends the current session - closes the CDP connection and clears tracked state. Safe to
    // call even if not connected.
    void Disconnect();

    [[nodiscard]] const GameTracker& GetTracker() const;
    [[nodiscard]] const BoardState& GetTrackedBoard() const;

    // Re-runs the site's extraction script and applies any moves new since the last call.
    // Returns them, in order, as UCI. Requests a fresh engine move once at the end if
    // anything was applied - never more than once per call, regardless of how many moves
    // were newly discovered.
    [[nodiscard]] std::vector<std::string> Poll();

    // True once Poll() found the site's move list shrink in a way that isn't a clean
    // new-game reset, or a SAN move failed to parse - tracking can't be trusted until
    // ConnectToSite() is called again.
    [[nodiscard]] bool HasDesynced() const;

private:
    void RequestEngineMove();

    EngineController* m_Controller = nullptr;
    BrowserLauncher m_Launcher;
    CdpClient m_CdpClient;
    ChessSite m_Site = ChessSite::ChessDotCom;
    ChessRules m_Rules;
    GameTracker m_Tracker;
    bool m_Connected = false;
    bool m_Desynced = false;
};
