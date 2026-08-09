#pragma once

#include "Browser/ChessSiteAdapter.h"

#include <array>
#include <string_view>

class EngineController;
class GameSession;

// The "Controls" window: engine restart, browser launch, site connect/disconnect, and the
// autoplay toggle. Owns the small bits of UI-only state (site selection, engine path text
// buffer, autoplay checkbox) that don't belong on EngineController/GameSession themselves -
// those only track state that's actually load-bearing for their own behavior.
class ControlsPanel
{
public:
    ControlsPanel(EngineController& controller, GameSession& gameSession);

    // Shuts down the engine (if running) and restarts it from enginePath, or the bundled
    // default if empty. Used both by the panel's own "Restart Engine" button and once by App
    // at startup to get the engine running before the first frame.
    void RestartEngine(std::string_view enginePath);

    // The engine path currently shown in the field - either the bundled default (nothing
    // customized/loaded) or whatever was last typed/picked or loaded from settings.ini. App
    // passes this to the startup RestartEngine() call so a persisted custom path takes effect
    // immediately rather than being silently overridden by the bundled default.
    [[nodiscard]] std::string_view GetEnginePath() const;

    // Writes every setting this panel owns to settings.ini (see ExecutablePathUtil::
    // GetSettingsFilePath()) next to the executable, overwriting whatever was there - the
    // counterpart to the constructor's LoadSettings(). Best-effort: logs and gives up on any
    // write failure rather than throwing. Called once by App at shutdown.
    void SaveSettings() const;

    // Not thread-safe: call once per frame from the UI thread only.
    void Draw();

private:
    // Restores every setting this panel owns from settings.ini, if it exists (a fresh install/
    // deleted file just keeps the in-class defaults - not an error). Called once from the
    // constructor, before the engine or GameSession are otherwise touched, so it only needs to
    // update its own members plus GameSession's mirroring setters directly (ApplyEloTarget's
    // EngineController half is a no-op before the engine has started - RestartEngine(), called
    // by App right after construction, re-applies it once the engine actually exists).
    void LoadSettings();


    // The single "Elo" control spans two independent things that both need reapplying
    // whenever the engine restarts (a fresh process starts unlimited): the engine's own
    // internal strength limiting (UCI_LimitStrength/UCI_Elo, sent directly via
    // EngineController::SetOption) and GameSession's derived movetime/depth preset (see
    // GameSession::SetEloTarget). Called both from the Elo widgets and from RestartEngine().
    void ApplyEloTarget();

    EngineController* m_Controller = nullptr;
    GameSession* m_GameSession = nullptr;

    ChessSite m_SelectedSite = ChessSite::ChessDotCom;
    bool m_AutoplayEnabled = false;
    bool m_BlitzMode = false;
    bool m_PremoveEnabled = false;

    bool m_LimitElo = false;
    int m_Elo = 1500;

    // Artificial pre-move pacing (see GameSession::SetMoveDelay) - m_MoveDelayMs is either the
    // fixed delay (m_RandomizeMoveDelay off) or the range's minimum (on), in which case
    // m_MoveDelayMaxMs is the range's maximum.
    int m_MoveDelayMs = 0;
    int m_MoveDelayMaxMs = 0;
    bool m_RandomizeMoveDelay = false;

    // Index into the parallel kHotkeyNames/kHotkeyKeys tables in ControlsPanel.cpp - which key
    // manually plays the current suggestion (see GameSession::PlayBestMoveNow) while autoplay
    // is off.
    int m_PlayMoveHotkeyIndex = 0;

    // Pre-filled with the bundled default so the field always shows what's actually running;
    // edit it and click Restart Engine to point at a different UCI-compatible executable.
    std::array<char, 512> m_EngineExecutablePathBuffer{};

    // Opening book (see GameSession::LoadOpeningBook/SetOpeningBookEnabled) - no bundled
    // default, unlike the engine path, so the buffer starts empty until the user browses to
    // one or a previous path is restored from settings.ini.
    bool m_OpeningBookEnabled = false;
    std::array<char, 512> m_BookPathBuffer{};
    // Index into kBookSelectionModeNames in ControlsPanel.cpp; 1 = weighted random (default).
    int m_BookSelectionModeIndex = 1;
};
