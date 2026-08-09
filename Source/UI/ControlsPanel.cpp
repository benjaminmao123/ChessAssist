#include "ControlsPanel.h"

#include "Engine/EngineController.h"
#include "Engine/ExecutablePathUtil.h"
#include "Game/GameSession.h"
#include "Logging/Log.h"
#include "ImguiUtils.h"

#include <imgui.h>

#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>

namespace
{
constexpr const char* kSiteNames[] = {"Chess.com", "Lichess"};
}  // namespace

ControlsPanel::ControlsPanel(EngineController& controller, GameSession& gameSession)
    : m_Controller(&controller), m_GameSession(&gameSession)
{
    const std::string defaultEnginePath = ExecutablePathUtil::GetDefaultStockfishPath().string();
    std::snprintf(m_EngineExecutablePathBuffer.data(), m_EngineExecutablePathBuffer.size(), "%s", defaultEnginePath.c_str());
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

    // A freshly spawned engine process starts with every UCI option at its default (i.e.
    // unlimited strength) - reapply whatever Elo target is currently configured so a restart
    // doesn't silently drop it.
    ApplyEloTarget();
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

void ControlsPanel::Draw()
{
    ImGui::Begin("Controls");

    ImGui::Text("Engine: %s", m_Controller->IsRunning() ? "running" : "not running");

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
    ImGui::InputText("Engine path", m_EngineExecutablePathBuffer.data(), m_EngineExecutablePathBuffer.size());
    if (ImGui::Button("Restart Engine"))
        RestartEngine(m_EngineExecutablePathBuffer.data());
    ImGui::EndDisabled();
    if (m_GameSession->IsConnected())
        ImGuiUtils::TextColorWrapped(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Disconnect to change engine");

    ImGui::Separator();

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

    ImGui::Separator();

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
        ImGui::TextWrapped("Log in and open a game in the ChessAssist browser window, then click Connect.");

        // A valid, in-sync session has nothing for another Connect click to do - and clicking
        // it anyway tears down the live CDP connection out from under any in-flight Poll(),
        // which is exactly what was surfacing as "CDP connection lost while waiting for
        // response". Re-connecting is only meaningful to establish a session or to resync
        // after a desync, so that's the only time the button's live.
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

    // Plays the engine's suggested move on the board automatically (drag-simulated via CDP)
    // whenever it's the tracked player's own turn. Intended for engine-vs-bot/engine testing,
    // not for use against a live human opponent - that would take the player out of the loop
    // entirely, against the fair-play rules of every site this connects to.
    if (ImGuiUtils::CheckboxTextWrapped("##Autoplay", &m_AutoplayEnabled, "Autoplay suggested moves"))
        m_GameSession->SetAutoplayEnabled(m_AutoplayEnabled);

    // Experimental - see GameSession::SetPremoveEnabled. Only meaningful with autoplay on;
    // shown regardless so the setting isn't lost if autoplay gets toggled off and back on.
    if (ImGuiUtils::CheckboxTextWrapped("##PremoveEnabled", &m_PremoveEnabled, "Enable premoves (experimental)"))
        m_GameSession->SetPremoveEnabled(m_PremoveEnabled);

    ImGui::End();
}
