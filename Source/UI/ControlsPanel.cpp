#include "ControlsPanel.h"

#include "Chess/PolyglotBook.h"
#include "Engine/EngineController.h"
#include "Engine/ExecutablePathUtil.h"
#include "Game/GameSession.h"
#include "ImguiUtils.h"
#include "Logging/Log.h"
#include "Settings/SettingsIni.h"

#include <imgui.h>
#include <ini/ini.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>

namespace
{
constexpr const char* kSiteNames[] = {"Chess.com", "Lichess"};

// Parallel tables for the "play current suggestion now" hotkey combo - kept short/common
// rather than exhaustive so the dropdown stays a quick pick, not a full key-capture UI.
constexpr const char* kHotkeyNames[] = {"Space", "Enter", "Tab", "F1", "F2", "F3", "F4"};
constexpr ImGuiKey kHotkeyKeys[] = {ImGuiKey_Space, ImGuiKey_Enter, ImGuiKey_Tab, ImGuiKey_F1, ImGuiKey_F2, ImGuiKey_F3, ImGuiKey_F4};

// Index 0/1 must match GameSession::PolyglotBook::SelectionMode's HighestWeight/WeightedRandom
// ordering - see the combo's on-change handler in Draw().
constexpr const char* kBookSelectionModeNames[] = {"Always best", "Weighted random"};
}  // namespace

ControlsPanel::ControlsPanel(EngineController& controller, GameSession& gameSession)
    : m_Controller(&controller), m_GameSession(&gameSession)
{
    const std::string defaultEnginePath = ExecutablePathUtil::GetDefaultStockfishPath().string();
    std::snprintf(m_EngineExecutablePathBuffer.data(), m_EngineExecutablePathBuffer.size(), "%s", defaultEnginePath.c_str());

    LoadSettings();
}

void ControlsPanel::RestartEngine(std::string_view enginePath)
{
    m_Controller->Shutdown();

    const std::optional<std::filesystem::path> path = enginePath.empty() ? std::nullopt : std::optional<std::filesystem::path>(std::filesystem::path(enginePath));

    if (const auto startResult = m_Controller->Start(path); !startResult)
    {
        LOG_ERROR("Failed to start engine: {}", startResult.error().Message);
        return;
    }

    LOG_INFO("Engine started from {}", path ? path->string() : "bundled default (" + ExecutablePathUtil::GetDefaultStockfishPath().string() + ")");

    // A freshly spawned engine process starts with every UCI option at its default (unlimited
    // strength) - reapply whatever Elo target is currently configured so a restart doesn't
    // silently drop it.
    ApplyEloTarget();

    // Asks the engine to compute GameSession::kMultiPvLines candidate lines instead of just the
    // best one, feeding GameSession::GetAlternateMoves() - same "options reset on every fresh
    // process" reasoning as ApplyEloTarget() above.
    GameSession::ConfigureMultiPv(*m_Controller);
}

void ControlsPanel::ApplyEloTarget()
{
    if (m_LimitElo)
    {
        m_Controller->SetOption("UCI_LimitStrength", "true");
        m_Controller->SetOption("UCI_Elo", std::to_string(m_Elo));
        m_GameSession->SetEloTarget(m_Elo);
    }
    else
    {
        m_Controller->SetOption("UCI_LimitStrength", "false");
        m_GameSession->SetEloTarget(std::nullopt);
    }
}

std::string_view ControlsPanel::GetEnginePath() const
{
    return m_EngineExecutablePathBuffer.data();
}

void ControlsPanel::LoadSettings()
{
    const std::string path = ExecutablePathUtil::GetSettingsFilePath().string();
    if (!std::filesystem::exists(path))
        return;

    try
    {
        const inih::INIReader ini(path);

        const std::string enginePath = ini.Get<std::string>("Engine", "Path", "");
        if (!enginePath.empty())
            std::snprintf(m_EngineExecutablePathBuffer.data(), m_EngineExecutablePathBuffer.size(), "%s", enginePath.c_str());

        m_SelectedSite = static_cast<ChessSite>(std::clamp(ini.Get<int>("Connection", "Site", static_cast<int>(m_SelectedSite)), 0, IM_ARRAYSIZE(kSiteNames) - 1));

        // Get<T>'s default-value parameter is T&& with T fixed (not deduced), so it binds only
        // to rvalues - static_cast produces one from each lvalue member (plain `m_LimitElo`
        // wouldn't compile).
        m_LimitElo = ini.Get<bool>("Strength", "LimitElo", static_cast<bool>(m_LimitElo));
        m_Elo = std::clamp(ini.Get<int>("Strength", "Elo", static_cast<int>(m_Elo)), GameSession::kMinElo, GameSession::kMaxElo);
        m_BlitzMode = ini.Get<bool>("Strength", "BlitzMode", static_cast<bool>(m_BlitzMode));

        m_AutoplayEnabled = ini.Get<bool>("Autoplay", "Enabled", static_cast<bool>(m_AutoplayEnabled));
        m_PremoveEnabled = ini.Get<bool>("Autoplay", "Premove", static_cast<bool>(m_PremoveEnabled));
        m_RandomizeMoveDelay = ini.Get<bool>("Autoplay", "RandomizeDelay", static_cast<bool>(m_RandomizeMoveDelay));
        m_MoveDelayMs = std::clamp(ini.Get<int>("Autoplay", "MoveDelayMs", static_cast<int>(m_MoveDelayMs)), 0, 10000);
        m_MoveDelayMaxMs = std::clamp(ini.Get<int>("Autoplay", "MoveDelayMaxMs", static_cast<int>(m_MoveDelayMaxMs)), 0, 10000);

        m_PlayMoveHotkeyIndex = std::clamp(ini.Get<int>("ManualPlay", "HotkeyIndex", static_cast<int>(m_PlayMoveHotkeyIndex)), 0, IM_ARRAYSIZE(kHotkeyNames) - 1);

        const std::string bookPath = ini.Get<std::string>("OpeningBook", "Path", "");
        if (!bookPath.empty())
            std::snprintf(m_BookPathBuffer.data(), m_BookPathBuffer.size(), "%s", bookPath.c_str());
        m_OpeningBookEnabled = ini.Get<bool>("OpeningBook", "Enabled", static_cast<bool>(m_OpeningBookEnabled));
        m_BookSelectionModeIndex = std::clamp(ini.Get<int>("OpeningBook", "SelectionMode", static_cast<int>(m_BookSelectionModeIndex)), 0, IM_ARRAYSIZE(kBookSelectionModeNames) - 1);
    }
    catch (const std::exception& e)
    {
        LOG_WARN("LoadSettings: failed to read '{}': {} - using defaults", path, e.what());
        return;
    }

    // Mirror every loaded value into GameSession, the same way each widget's own on-change
    // handler would in Draw(). The Elo/EngineController half (UCI_Elo etc.) is deliberately NOT
    // done here since the engine hasn't started yet - App's startup RestartEngine() call covers
    // that via its own ApplyEloTarget(). Loading the book is pure file parsing with no process
    // dependency, so it can happen right here instead.
    m_GameSession->SetBlitzMode(m_BlitzMode);
    m_GameSession->SetAutoplayEnabled(m_AutoplayEnabled);
    m_GameSession->SetPremoveEnabled(m_PremoveEnabled);
    m_GameSession->SetMoveDelay(m_MoveDelayMs, m_RandomizeMoveDelay ? m_MoveDelayMaxMs : m_MoveDelayMs);
    m_GameSession->SetBookSelectionMode(m_BookSelectionModeIndex == 0 ? PolyglotBook::SelectionMode::HighestWeight : PolyglotBook::SelectionMode::WeightedRandom);
    if (m_BookPathBuffer[0] != '\0')
        m_GameSession->LoadOpeningBook(m_BookPathBuffer.data());
    m_GameSession->SetOpeningBookEnabled(m_OpeningBookEnabled);

    LOG_INFO("LoadSettings: restored settings from {}", path);
}

void ControlsPanel::SaveSettings() const
{
    const std::string path = ExecutablePathUtil::GetSettingsFilePath().string();

    // Read-merge rather than starting from a blank INIReader: INIWriter::write() always writes
    // out exactly (and only) what's in the INIReader object it's given, so starting blank would
    // silently erase BoardStatePanel::SaveSettings()'s "Display" section in this same file.
    // BoardStatePanel::SaveSettings() does the identical read-merge in the other direction.
    inih::INIReader ini = SettingsIni::LoadOrEmpty(path, "SaveSettings");

    SettingsIni::UpsertEntry(ini, "Engine", "Path", std::string(m_EngineExecutablePathBuffer.data()));
    SettingsIni::UpsertEntry(ini, "Connection", "Site", static_cast<int>(m_SelectedSite));
    SettingsIni::UpsertEntry(ini, "Strength", "LimitElo", m_LimitElo);
    SettingsIni::UpsertEntry(ini, "Strength", "Elo", m_Elo);
    SettingsIni::UpsertEntry(ini, "Strength", "BlitzMode", m_BlitzMode);
    SettingsIni::UpsertEntry(ini, "Autoplay", "Enabled", m_AutoplayEnabled);
    SettingsIni::UpsertEntry(ini, "Autoplay", "Premove", m_PremoveEnabled);
    SettingsIni::UpsertEntry(ini, "Autoplay", "RandomizeDelay", m_RandomizeMoveDelay);
    SettingsIni::UpsertEntry(ini, "Autoplay", "MoveDelayMs", m_MoveDelayMs);
    SettingsIni::UpsertEntry(ini, "Autoplay", "MoveDelayMaxMs", m_MoveDelayMaxMs);
    SettingsIni::UpsertEntry(ini, "ManualPlay", "HotkeyIndex", m_PlayMoveHotkeyIndex);
    SettingsIni::UpsertEntry(ini, "OpeningBook", "Enabled", m_OpeningBookEnabled);
    SettingsIni::UpsertEntry(ini, "OpeningBook", "Path", std::string(m_BookPathBuffer.data()));
    SettingsIni::UpsertEntry(ini, "OpeningBook", "SelectionMode", m_BookSelectionModeIndex);

    SettingsIni::SaveMerged(path, ini, "SaveSettings");
}

void ControlsPanel::Draw()
{
    ImGui::Begin("Controls");

    ImGui::SeparatorText("Engine");

    ImGui::TextColored(m_Controller->IsRunning() ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Engine: %s", m_Controller->IsRunning() ? "running" : "not running");

    // Swapping the engine out from under an in-progress game would silently reset whatever
    // analysis state the new process starts with mid-position - require disconnecting first.
    ImGui::BeginDisabled(m_GameSession->IsConnected());
    if (ImGui::Button("..."))
    {
        const std::optional<std::filesystem::path> newPath = ExecutablePathUtil::PromptForEnginePath();
        if (newPath)
            std::snprintf(m_EngineExecutablePathBuffer.data(), m_EngineExecutablePathBuffer.size(), "%s", newPath->string().c_str());
    }
    ImGui::SameLine();
    ImGui::InputText("Path", m_EngineExecutablePathBuffer.data(), m_EngineExecutablePathBuffer.size());
    if (ImGui::Button("Restart Engine"))
        RestartEngine(m_EngineExecutablePathBuffer.data());
    ImGui::EndDisabled();
    if (m_GameSession->IsConnected())
        ImGuiUtils::TextColorWrapped(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Disconnect to change engine");

    ImGui::SeparatorText("Strength & Speed");

    // Limits both the engine's own play (UCI_LimitStrength/UCI_Elo) and, roughly, how long/
    // deep it searches (see GameSession::SetEloTarget) - a single control for "make it play
    // like a ~1500", rather than fiddling with search time/depth by hand.
    if (ImGuiUtils::CheckboxTextWrapped("##LimitElo", &m_LimitElo, "Limit Elo"))
        ApplyEloTarget();

    ImGui::BeginDisabled(!m_LimitElo);
    if (ImGui::SliderInt("Elo", &m_Elo, GameSession::kMinElo, GameSession::kMaxElo))
        ApplyEloTarget();
    ImGui::EndDisabled();

    // Blitz overrides SetEloTarget's preset (or the no-cap default) with a short fixed search
    // (see GameSession::SetBlitzMode) regardless of the Elo setting above - independent knob
    // for "make it fast" vs. "make it weak".
    if (ImGuiUtils::CheckboxTextWrapped("##BlitzMode", &m_BlitzMode, "Blitz mode (fast, shallow searches)"))
        m_GameSession->SetBlitzMode(m_BlitzMode);

    ImGui::SeparatorText("Opening Book");

    // Plays moves straight from a loaded Polyglot book, skipping engine search entirely,
    // whenever the current position has an entry - see GameSession::RequestEngineMove's book
    // check. Falls back to normal search the instant the position leaves the book, so nothing
    // else here needs to know or care whether a given move came from the book or the engine.
    if (ImGuiUtils::CheckboxTextWrapped("##OpeningBookEnabled", &m_OpeningBookEnabled, "Use opening book"))
        m_GameSession->SetOpeningBookEnabled(m_OpeningBookEnabled);

    if (ImGui::Button("...##BookPath"))
    {
        const std::optional<std::filesystem::path> newPath = ExecutablePathUtil::PromptForBookPath();
        if (newPath)
            std::snprintf(m_BookPathBuffer.data(), m_BookPathBuffer.size(), "%s", newPath->string().c_str());
    }
    ImGui::SameLine();
    ImGui::InputText("Path##BookPath", m_BookPathBuffer.data(), m_BookPathBuffer.size());
    if (ImGui::Button("Load Book"))
    {
        if (m_GameSession->LoadOpeningBook(m_BookPathBuffer.data()))
            LOG_INFO("Book loaded");
        else
            LOG_ERROR("Failed to load book - see Log.");
    }

    ImGui::TextWrapped("Move choice");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##BookSelectionMode", &m_BookSelectionModeIndex, kBookSelectionModeNames, IM_ARRAYSIZE(kBookSelectionModeNames)))
        m_GameSession->SetBookSelectionMode(m_BookSelectionModeIndex == 0 ? PolyglotBook::SelectionMode::HighestWeight : PolyglotBook::SelectionMode::WeightedRandom);

    if (m_GameSession->HasOpeningBookLoaded())
        ImGuiUtils::TextColorWrapped(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Book loaded");
    else
        ImGuiUtils::TextColorWrapped(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No book loaded");

    ImGui::SeparatorText("Connection");

    int siteIndex = static_cast<int>(m_SelectedSite);
    if (ImGui::Combo("Site", &siteIndex, kSiteNames, IM_ARRAYSIZE(kSiteNames)))
        m_SelectedSite = static_cast<ChessSite>(siteIndex);

    if (!m_GameSession->IsBrowserRunning())
    {
        if (ImGui::Button("Launch Browser"))
        {
            if (const auto launched = m_GameSession->LaunchBrowser(ExecutablePathUtil::GetBrowserProfileDirectory(), m_SelectedSite); !launched)
                LOG_ERROR("Failed to launch browser: {}", launched.error().Message);
            else
                LOG_INFO("Browser launched - log in and open a game, then click Connect.");
        }
    }
    else
    {
        ImGui::TextWrapped("Log in and open a game in the Chess Assist browser window, then click Connect.");

        // A valid, in-sync session has nothing for another Connect click to do - clicking it
        // anyway tears down the live CDP connection out from under any in-flight Poll(), which
        // was surfacing as "CDP connection lost while waiting for response". Only live when it's
        // meaningful: establishing a session, or resyncing after a desync.
        ImGui::BeginDisabled(m_GameSession->IsConnected() && !m_GameSession->HasDesynced());
        if (ImGui::Button("Connect"))
        {
            if (m_GameSession->ConnectToSite(m_SelectedSite))
                LOG_INFO("Connected - watching {}", kSiteNames[static_cast<int>(m_SelectedSite)]);
            else
                LOG_ERROR("Failed to connect - see Log.");
        }
        ImGui::EndDisabled();

        if (m_GameSession->IsConnected())
        {
            ImGui::SameLine();
            if (ImGui::Button("Disconnect"))
            {
                m_GameSession->Disconnect();
                LOG_INFO("Disconnected");
            }
        }
    }

    if (m_GameSession->IsConnected())
    {
        if (m_GameSession->HasDesynced())
        {
            ImGuiUtils::TextColorWrapped(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Tracking lost sync - click Connect to resync");
        }
        else
            ImGuiUtils::TextColorWrapped(ImGui::GetStyleColorVec4(ImGuiCol_Text), "Connected - watching %s - %zu move(s) recorded", kSiteNames[static_cast<int>(m_SelectedSite)], m_GameSession->GetTracker().GetMoves().size());
    }
    else
    {
        ImGuiUtils::TextColorWrapped(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "Not connected - launch the browser and click Connect to start.");
    }

    ImGui::SeparatorText("Autoplay");

    // Plays the engine's suggested move on the board automatically (drag-simulated via CDP)
    // whenever it's the tracked player's own turn. Intended for engine-vs-bot/engine testing,
    // not for use against a live human opponent - that would take the player out of the loop
    // entirely, against the fair-play rules of every site this connects to.
    if (ImGuiUtils::CheckboxTextWrapped("##Autoplay", &m_AutoplayEnabled, "Autoplay suggested moves"))
        m_GameSession->SetAutoplayEnabled(m_AutoplayEnabled);

    // Experimental - see GameSession::SetPremoveEnabled. Only meaningful with autoplay on;
    // shown regardless so the setting isn't lost if autoplay gets toggled off and back on.
    if (ImGuiUtils::CheckboxTextWrapped("##PremoveEnabled", &m_PremoveEnabled, "Enable premoves (autoplay only)"))
        m_GameSession->SetPremoveEnabled(m_PremoveEnabled);

    ImGui::Separator();

    // Artificial pacing before autoplay actually plays a decided move on the board - layered
    // on top of (not instead of) the engine's own think time (Elo/Blitz above); see
    // GameSession::SetMoveDelay. Doesn't apply to the premove fast-path, which is deliberately
    // instant.
    bool delayChanged = ImGuiUtils::CheckboxTextWrapped("##RandomizeMoveDelay", &m_RandomizeMoveDelay, "Randomize wait time");

    if (m_RandomizeMoveDelay)
    {
        if (m_MoveDelayMaxMs < m_MoveDelayMs)
            m_MoveDelayMaxMs = m_MoveDelayMs;

        // Native ImGui widget labels are drawn on a single line to the right of the widget and
        // don't wrap - too easy to clip in this panel's narrow column, so the label is drawn
        // separately (wrapped) above the widget instead, with the widget's own label hidden.
        ImGui::TextWrapped("Wait range (ms)");
        ImGui::SetNextItemWidth(-FLT_MIN);
        delayChanged |= ImGui::DragIntRange2("##WaitRangeMs", &m_MoveDelayMs, &m_MoveDelayMaxMs, 10.0f, 0, 10000, "Min: %d ms", "Max: %d ms");
    }
    else
    {
        ImGui::TextWrapped("Wait before playing (ms)");
        ImGui::SetNextItemWidth(-FLT_MIN);
        delayChanged |= ImGui::SliderInt("##WaitMs", &m_MoveDelayMs, 0, 10000);
    }

    if (delayChanged)
        m_GameSession->SetMoveDelay(m_MoveDelayMs, m_RandomizeMoveDelay ? m_MoveDelayMaxMs : m_MoveDelayMs);

    ImGui::SeparatorText("Manual Play");

    // Manual "play the current suggestion now" hotkey - only fires while autoplay is off
    // (with it on, moves already play themselves per the wait-time setting above) and while
    // this window/app isn't capturing text input, so it doesn't fire while e.g. editing the
    // engine path field above.
    ImGui::BeginDisabled(m_AutoplayEnabled);
    ImGui::TextWrapped("Play-move hotkey");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::Combo("##PlayMoveHotkey", &m_PlayMoveHotkeyIndex, kHotkeyNames, IM_ARRAYSIZE(kHotkeyNames));
    ImGui::EndDisabled();

    if (!m_AutoplayEnabled && m_GameSession->IsConnected() && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(kHotkeyKeys[m_PlayMoveHotkeyIndex], false))
        m_GameSession->PlayBestMoveNow();

    ImGui::End();
}
