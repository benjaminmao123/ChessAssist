#include "AnalysisBoardPanel.h"

#include "ChessPieceTextures.h"
#include "Engine/ExecutablePathUtil.h"
#include "Game/AnalysisBoardSession.h"
#include "ImguiUtils.h"
#include "Logging/Log.h"
#include "Settings/SettingsIni.h"
#include "UI/EngineInfoPanel.h"

#include <imgui.h>
#include <ini/ini.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>

AnalysisBoardPanel::AnalysisBoardPanel(EngineInfoPanel& enginePanel, AnalysisBoardSession& session, const ChessPieceTextures& textures)
    : m_EnginePanel(&enginePanel), m_Session(&session), m_Textures(&textures)
{
    LoadSettings();
}

void AnalysisBoardPanel::LoadSettings()
{
    const std::string path = ExecutablePathUtil::GetSettingsFilePath().string();
    if (!std::filesystem::exists(path))
        return;

    try
    {
        const inih::INIReader ini(path);

        // See BoardStatePanel::LoadSettings()'s comment for why the static_cast is needed here.
        m_ShowLookaheadArrow = ini.Get<bool>("AnalysisBoard", "ShowLookaheadArrow", static_cast<bool>(m_ShowLookaheadArrow));
        m_ShowAlternateMoves = ini.Get<bool>("AnalysisBoard", "ShowAlternateMoves", static_cast<bool>(m_ShowAlternateMoves));
    }
    catch (const std::exception& e)
    {
        LOG_WARN("AnalysisBoardPanel::LoadSettings: failed to read '{}': {} - using defaults", path, e.what());
    }
}

void AnalysisBoardPanel::SaveSettings() const
{
    const std::string path = ExecutablePathUtil::GetSettingsFilePath().string();

    // Read-merge rather than starting from a blank INIReader - same reasoning as
    // BoardStatePanel::SaveSettings()'s own comment, just for this panel's own "AnalysisBoard"
    // section.
    inih::INIReader ini = SettingsIni::LoadOrEmpty(path, "AnalysisBoardPanel::SaveSettings");

    SettingsIni::UpsertEntry(ini, "AnalysisBoard", "ShowLookaheadArrow", m_ShowLookaheadArrow);
    SettingsIni::UpsertEntry(ini, "AnalysisBoard", "ShowAlternateMoves", m_ShowAlternateMoves);

    SettingsIni::SaveMerged(path, ini, "AnalysisBoardPanel::SaveSettings");
}

void AnalysisBoardPanel::Draw()
{
    ImGui::Begin("Analysis Board");

    ImGuiUtils::CheckboxTextWrapped("##ShowLookaheadArrow", &m_ShowLookaheadArrow, "Show lookahead arrow");
    ImGui::SameLine();
    ImGuiUtils::CheckboxTextWrapped("##ShowAlternateMoves", &m_ShowAlternateMoves, "Show alternate moves");
    ImGui::Separator();

    if (ImGui::Button("Reset"))
        m_Session->Reset();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Back to move 0 of this position");
    ImGui::SameLine();
    if (ImGui::Button("Standard Start"))
        m_Session->ResetToStandardStartingPosition();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Discard any loaded FEN and start a fresh game");
    ImGui::SameLine();
    if (ImGui::Button("Flip Board"))
        m_Session->FlipBoard();
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_Session->CanStepBackward());
    if (ImGui::Button("<"))
        m_Session->StepBackward();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_Session->CanStepForward());
    if (ImGui::Button(">"))
        m_Session->StepForward();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Text("Move %zu / %zu", m_Session->GetCursor(), m_Session->HistoryLength());

    // Arrow-key navigation - gated on this window having focus (so it doesn't fire while e.g.
    // the Controls panel is focused) and on not currently capturing text input (so it doesn't
    // fire while typing into the FEN field below), same pattern ControlsPanel's own manual-play
    // hotkey uses.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::GetIO().WantTextInput)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
            m_Session->StepBackward();
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
            m_Session->StepForward();
    }

    // Current-position FEN (see AnalysisBoardSession::GetFen()) - read-only but still
    // selectable/copyable (Ctrl+C, or drag-select) since it's a real InputText rather than
    // plain Text, plus an explicit Copy button for anyone who'd rather click. Recomputed fresh
    // every frame (cheap - just a board scan), so it's always in sync with whatever's currently
    // displayed, including hypothetical moves and pasted FEN alike.
    const std::string currentFen = m_Session->GetFen();
    char fenDisplayBuffer[128];
    std::snprintf(fenDisplayBuffer, sizeof(fenDisplayBuffer), "%s", currentFen.c_str());
    // A plain -FLT_MIN width here would claim the entire row, pushing the Copy FEN button past
    // the window's right edge - reserve exactly the button's own width (plus the gap
    // ImGui::SameLine() leaves) instead, the standard "text field with a trailing button"
    // idiom (see ImGui::PushItemWidth's own docs on negative widths).
    const float copyButtonWidth = ImGui::CalcTextSize("Copy FEN").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(-(copyButtonWidth + ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputText("##CurrentFen", fenDisplayBuffer, sizeof(fenDisplayBuffer), ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    if (ImGui::Button("Copy FEN"))
        ImGui::SetClipboardText(currentFen.c_str());

    // FEN paste - sets the position directly, discarding whatever was played before (see
    // AnalysisBoardSession::LoadFen()'s comment). Enter submits, same as the field's own text
    // cursor leaving it; the button is an explicit alternative for anyone who'd rather click.
    // Same trailing-button width reservation as the current-FEN field above.
    const float loadButtonWidth = ImGui::CalcTextSize("Load FEN").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(-(loadButtonWidth + ImGui::GetStyle().ItemSpacing.x));
    const bool fenSubmitted = ImGui::InputTextWithHint("##FenInput", "Paste FEN and press Enter...", m_FenBuffer.data(), m_FenBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemEdited())
        m_FenLoadFailed = false;  // stop showing a stale error the moment the user starts typing again
    ImGui::SameLine();
    const bool fenButtonPressed = ImGui::Button("Load FEN");
    if (fenSubmitted || fenButtonPressed)
        m_FenLoadFailed = !m_Session->LoadFen(m_FenBuffer.data());
    if (m_FenLoadFailed)
        ImGuiUtils::TextColorWrapped(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Invalid FEN");

    ImGui::Separator();

    const std::optional<EngineInfoPanel::MateInfo> mateInfo = m_EnginePanel->GetMateInfo();

    // Fills whatever space the panel actually has - same layout approach as BoardStatePanel's
    // own board (see its comment). The controls/FEN rows above are already excluded from
    // available.y automatically (GetContentRegionAvail() is queried after they're drawn, so
    // ImGui's own cursor tracking already accounts for their height) - reservedForText only
    // needs to cover what's drawn *after* the board via Dummy()/direct draw calls:
    // EngineInfoPanel::DrawContents()'s Depth/Score/Nodes/Nps/PV(up to 2 wrapped lines)/
    // separator/Best move, the same ~8 lines BoardStatePanel reserves 9 for minus its Accuracy
    // line (this panel has no accuracy concept).
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float reservedForText = ImGui::GetTextLineHeightWithSpacing() * 8.0f;
    const float availableForBoardWidth = available.x - kChessBoardEvalBarWidth - kChessBoardEvalBarGap;
    const float availableForBoardHeight = available.y - reservedForText;
    const float squareSize = std::max(std::min(availableForBoardWidth, availableForBoardHeight) / 8.0f, kChessBoardMinSquareSize);
    const float boardSize = squareSize * 8.0f;

    const ImVec2 panelOrigin = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const ImVec2 barMin = panelOrigin;
    const ImVec2 barMax(panelOrigin.x + kChessBoardEvalBarWidth, panelOrigin.y + boardSize);
    ChessBoardDrawEvalBar(drawList, barMin, barMax, m_EnginePanel->GetWhiteWinFraction(), m_Session->IsBlackAtBottom());

    const ImVec2 boardOrigin(panelOrigin.x + kChessBoardEvalBarWidth + kChessBoardEvalBarGap, panelOrigin.y);
    const ImVec2 boardEnd(boardOrigin.x + boardSize, boardOrigin.y + boardSize);

    const bool blackAtBottom = m_Session->IsBlackAtBottom();
    const BoardState& board = m_Session->GetBoard();

    m_Widget.ClearAnnotationsIfBoardChanged(board);

    const ImVec2 mousePos = ImGui::GetIO().MousePos;
    const std::optional<int> squareUnderMouse = ChessBoardSquareAtScreenPos(mousePos, boardOrigin, squareSize, blackAtBottom);
    const bool windowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    m_Widget.UpdateInteraction(*m_Session, squareUnderMouse, windowHovered);
    m_Widget.DrawBoardAndPieces(drawList, *m_Textures, board, blackAtBottom, boardOrigin, boardEnd, squareSize, mousePos);
    m_Widget.DrawDragHighlights(drawList, board, blackAtBottom, boardOrigin, squareSize);

    const std::optional<ChessBoardSuggestedSquares> suggested = ChessBoardComputeSuggestedSquares(m_Session->GetSuggestedMove(), blackAtBottom, boardOrigin, squareSize);
    if (suggested)
    {
        drawList->AddRectFilled(suggested->FromMin, suggested->FromMax, kChessBoardSourceHighlightColor);
        drawList->AddRectFilled(suggested->ToMin, suggested->ToMax, kChessBoardDestHighlightColor);
    }

    const std::optional<int> checkedKingSquare = m_Session->GetCheckedKingSquare();
    if (checkedKingSquare)
    {
        const ImVec2 checkMin = ChessBoardSquareMin(*checkedKingSquare % 8, *checkedKingSquare / 8, blackAtBottom, boardOrigin, squareSize);
        const ImVec2 checkMax(checkMin.x + squareSize, checkMin.y + squareSize);
        drawList->AddRectFilled(checkMin, checkMax, kChessBoardCheckHighlightColor);
    }

    // Alternate candidate moves (see AnalysisBoardSession::GetAlternateMoves()) - "other possible
    // moves that aren't necessarily the best," each its own color, drawn first/thinnest so the
    // lookahead and primary arrows both still read as more prominent on top of them. Gated
    // behind m_ShowAlternateMoves, same as BoardStatePanel's own.
    if (m_ShowAlternateMoves)
    {
        const std::vector<std::string> alternateMoves = m_Session->GetAlternateMoves();
        const float alternateThickness = std::max(squareSize * 0.07f, 2.0f);
        for (std::size_t i = 0; i < alternateMoves.size(); ++i)
        {
            const std::optional<ChessBoardSuggestedSquares> alternate = ChessBoardComputeSuggestedSquares(alternateMoves[i], blackAtBottom, boardOrigin, squareSize);
            if (!alternate)
                continue;

            const ImU32 color = kChessBoardAlternateArrowColors[std::min(i, std::size(kChessBoardAlternateArrowColors) - 1)];
            ChessBoardDrawArrow(drawList, alternate->FromCenter, alternate->ToCenter, color, alternateThickness);
        }
    }

    // Lookahead arrow drawn before the primary one so the primary arrow still reads as most
    // prominent - the anticipated reply to the primary suggestion (see AnalysisBoardSession::
    // GetLookaheadMove()). Gated behind m_ShowLookaheadArrow, same as BoardStatePanel's own.
    if (m_ShowLookaheadArrow)
    {
        const std::optional<ChessBoardSuggestedSquares> lookahead = ChessBoardComputeSuggestedSquares(m_Session->GetLookaheadMove(), blackAtBottom, boardOrigin, squareSize);
        if (lookahead)
            ChessBoardDrawArrow(drawList, lookahead->FromCenter, lookahead->ToCenter, kChessBoardLookaheadArrowColor, std::max(squareSize * 0.08f, 2.5f));
    }

    if (suggested)
        ChessBoardDrawArrow(drawList, suggested->FromCenter, suggested->ToCenter, mateInfo ? kChessBoardMatingArrowColor : kChessBoardArrowColor, std::max(squareSize * 0.1f, 3.0f));

    m_Widget.DrawAnnotationArrows(drawList, blackAtBottom, boardOrigin, squareSize, squareUnderMouse);

    if (mateInfo)
    {
        char banner[48];
        std::snprintf(banner, sizeof(banner), "%s mates in %d", mateInfo->WhiteIsMating ? "White" : "Black", mateInfo->DistanceInMoves);
        const ImVec2 textSize = ImGui::CalcTextSize(banner);
        const ImVec2 bannerMin(boardOrigin.x + 4.0f, boardOrigin.y + 4.0f);
        const ImVec2 bannerMax(bannerMin.x + textSize.x + 12.0f, bannerMin.y + textSize.y + 8.0f);
        drawList->AddRectFilled(bannerMin, bannerMax, kChessBoardMateBannerBg, 4.0f);
        drawList->AddText(ImVec2(bannerMin.x + 6.0f, bannerMin.y + 4.0f), kChessBoardMateBannerText, banner);
    }

    ImGui::Dummy(ImVec2(kChessBoardEvalBarWidth + kChessBoardEvalBarGap + boardSize, boardSize));

    m_Widget.DrawPromotionPopup(*m_Session, *m_Textures, blackAtBottom, boardOrigin, squareSize);

    ImGui::Separator();
    m_EnginePanel->DrawContents();

    ImGui::End();
}
