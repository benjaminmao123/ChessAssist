#include "BoardStatePanel.h"

#include "ChessPieceTextures.h"
#include "Engine/ExecutablePathUtil.h"
#include "Game/SandboxSession.h"
#include "ImguiUtils.h"
#include "Logging/Log.h"
#include "Settings/SettingsIni.h"
#include "UI/EngineInfoPanel.h"

#include <imgui.h>
#include <ini/ini.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>

BoardStatePanel::BoardStatePanel(EngineInfoPanel& liveEnginePanel, EngineInfoPanel& sandboxEnginePanel, SandboxSession& sandbox, const ChessPieceTextures& textures)
    : m_LiveEnginePanel(&liveEnginePanel), m_SandboxEnginePanel(&sandboxEnginePanel), m_Sandbox(&sandbox), m_Textures(&textures)
{
    LoadSettings();
}

void BoardStatePanel::LoadSettings()
{
    const std::string path = ExecutablePathUtil::GetSettingsFilePath().string();
    if (!std::filesystem::exists(path))
        return;

    try
    {
        const inih::INIReader ini(path);

        // Get<T>'s default-value parameter is T&& with T fixed (not deduced), so it binds only
        // to rvalues - static_cast produces one from each lvalue member. Same pattern
        // ControlsPanel::LoadSettings() uses for its own bool settings.
        m_ShowLookaheadArrow = ini.Get<bool>("Display", "ShowLookaheadArrow", static_cast<bool>(m_ShowLookaheadArrow));
        m_ShowAlternateMoves = ini.Get<bool>("Display", "ShowAlternateMoves", static_cast<bool>(m_ShowAlternateMoves));
        m_Open = ini.Get<bool>("Window", "LiveAnalysisBoardOpen", static_cast<bool>(m_Open));
    }
    catch (const std::exception& e)
    {
        LOG_WARN("BoardStatePanel::LoadSettings: failed to read '{}': {} - using defaults", path, e.what());
    }
}

void BoardStatePanel::SaveSettings() const
{
    const std::string path = ExecutablePathUtil::GetSettingsFilePath().string();

    // Read-merge rather than starting from a blank INIReader: INIWriter::write() always writes
    // out exactly (and only) what's in the INIReader object it's given, so starting blank would
    // silently erase every section ControlsPanel::SaveSettings() owns in this same file. This
    // panel only ever touches its own "Display" section; ControlsPanel does the identical
    // read-merge in the other direction.
    inih::INIReader ini = SettingsIni::LoadOrEmpty(path, "BoardStatePanel::SaveSettings");

    SettingsIni::UpsertEntry(ini, "Display", "ShowLookaheadArrow", m_ShowLookaheadArrow);
    SettingsIni::UpsertEntry(ini, "Display", "ShowAlternateMoves", m_ShowAlternateMoves);
    SettingsIni::UpsertEntry(ini, "Window", "LiveAnalysisBoardOpen", m_Open);

    SettingsIni::SaveMerged(path, ini, "BoardStatePanel::SaveSettings");
}

void BoardStatePanel::Draw(const std::optional<std::string>& liveSuggestedMove, const std::optional<std::string>& lookaheadMove, const std::vector<std::string>& alternateMoves, std::optional<float> accuracyPercent)
{
    if (!m_Open)
        return;

    ImGui::Begin("Live Analysis Board", &m_Open);

    // Display-only toggles for the two secondary arrow kinds below - live here (rather than
    // ControlsPanel) since they're purely about what this window draws, not engine behavior.
    ImGuiUtils::CheckboxTextWrapped("##ShowLookaheadArrow", &m_ShowLookaheadArrow, "Show lookahead arrow");
    ImGui::SameLine();
    ImGuiUtils::CheckboxTextWrapped("##ShowAlternateMoves", &m_ShowAlternateMoves, "Show alternate moves");
    ImGui::Separator();

    // Current-position FEN - read-only but still selectable/copyable (Ctrl+C, or drag-select)
    // since it's a real InputText rather than plain Text, plus an explicit Copy button. Same
    // pattern as AnalysisBoardPanel's own FEN field. Recomputed fresh every frame (cheap - just
    // a board scan), so it's always in sync, including while a sandbox line is active.
    const std::string currentFen = m_Sandbox->GetFen();
    char fenDisplayBuffer[128];
    std::snprintf(fenDisplayBuffer, sizeof(fenDisplayBuffer), "%s", currentFen.c_str());
    const float copyButtonWidth = ImGui::CalcTextSize("Copy FEN").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(-(copyButtonWidth + ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputText("##CurrentFen", fenDisplayBuffer, sizeof(fenDisplayBuffer), ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    if (ImGui::Button("Copy FEN"))
        ImGui::SetClipboardText(currentFen.c_str());
    ImGui::Separator();

    const bool sandboxActive = m_Sandbox->IsActive();
    EngineInfoPanel& activeEnginePanel = sandboxActive ? *m_SandboxEnginePanel : *m_LiveEnginePanel;

    // Sandbox controls row - only takes up layout space while there's something to control.
    if (sandboxActive)
    {
        ImGui::Text("Exploring %zu move(s)", m_Sandbox->HistoryLength());
        ImGui::SameLine();
        if (ImGui::Button("Undo"))
            m_Sandbox->UndoLastMove();
        ImGui::SameLine();
        if (ImGui::Button("Reset"))
            m_Sandbox->ResetToLive();
        ImGui::Separator();
    }

    const std::optional<EngineInfoPanel::MateInfo> mateInfo = activeEnginePanel.GetMateInfo();

    // Fills whatever space the panel actually has rather than a fixed pixel size, so the board
    // is as big as the window/dock layout allows. reservedForText approximates the height the
    // text below the board needs (Accuracy, then EngineInfoPanel::DrawContents()'s fields) in
    // units that scale with font size/UI scale rather than a hardcoded pixel guess.
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float reservedForText = ImGui::GetTextLineHeightWithSpacing() * 9.0f;
    const float availableForBoardWidth = available.x - kChessBoardEvalBarWidth - kChessBoardEvalBarGap;
    const float availableForBoardHeight = available.y - reservedForText;
    const float squareSize = std::max(std::min(availableForBoardWidth, availableForBoardHeight) / 8.0f, kChessBoardMinSquareSize);
    const float boardSize = squareSize * 8.0f;

    const ImVec2 panelOrigin = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const ImVec2 barMin = panelOrigin;
    const ImVec2 barMax(panelOrigin.x + kChessBoardEvalBarWidth, panelOrigin.y + boardSize);
    ChessBoardDrawEvalBar(drawList, barMin, barMax, activeEnginePanel.GetWhiteWinFraction(), m_Sandbox->IsBlackAtBottom());

    const ImVec2 boardOrigin(panelOrigin.x + kChessBoardEvalBarWidth + kChessBoardEvalBarGap, panelOrigin.y);
    const ImVec2 boardEnd(boardOrigin.x + boardSize, boardOrigin.y + boardSize);

    const bool blackAtBottom = m_Sandbox->IsBlackAtBottom();
    const BoardState& board = m_Sandbox->GetBoard();

    // User-drawn annotation arrows are a transient "let me visualize this" aid, not a permanent
    // record - drop them the moment the position changes (a move, an undo/reset, a resync).
    m_Widget.ClearAnnotationsIfBoardChanged(board);

    // Sandbox mouse interaction - a drag/click always targets the sandbox layer, seeding it (via
    // GameSession's live position, already mirrored in when inactive) on the first move, never
    // the live site.
    const ImVec2 mousePos = ImGui::GetIO().MousePos;
    const std::optional<int> squareUnderMouse = ChessBoardSquareAtScreenPos(mousePos, boardOrigin, squareSize, blackAtBottom);
    const bool windowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    m_Widget.UpdateInteraction(*m_Sandbox, squareUnderMouse, windowHovered);
    m_Widget.DrawBoardAndPieces(drawList, *m_Textures, board, blackAtBottom, boardOrigin, boardEnd, squareSize, mousePos);
    m_Widget.DrawDragHighlights(drawList, board, blackAtBottom, boardOrigin, squareSize);

    const std::optional<std::string> primarySuggestedMove = sandboxActive ? m_Sandbox->GetSuggestedMove() : liveSuggestedMove;
    const std::optional<ChessBoardSuggestedSquares> suggested = ChessBoardComputeSuggestedSquares(primarySuggestedMove, blackAtBottom, boardOrigin, squareSize);
    if (suggested)
    {
        drawList->AddRectFilled(suggested->FromMin, suggested->FromMax, kChessBoardSourceHighlightColor);
        drawList->AddRectFilled(suggested->ToMin, suggested->ToMax, kChessBoardDestHighlightColor);
    }

    const std::optional<int> checkedKingSquare = m_Sandbox->GetCheckedKingSquare();
    if (checkedKingSquare)
    {
        const ImVec2 checkMin = ChessBoardSquareMin(*checkedKingSquare % 8, *checkedKingSquare / 8, blackAtBottom, boardOrigin, squareSize);
        const ImVec2 checkMax(checkMin.x + squareSize, checkMin.y + squareSize);
        drawList->AddRectFilled(checkMin, checkMax, kChessBoardCheckHighlightColor);
    }

    // Alternate candidate moves - "other possible moves that aren't necessarily the best," each
    // its own color, drawn first/thinnest so the lookahead and primary arrows read as more
    // prominent on top of them. The sandbox's own dedicated engine gets the same MultiPV
    // treatment as the live one (see App). Gated behind m_ShowAlternateMoves.
    if (m_ShowAlternateMoves)
    {
        const std::vector<std::string> activeAlternateMoves = sandboxActive ? m_Sandbox->GetAlternateMoves() : alternateMoves;
        const float alternateThickness = std::max(squareSize * 0.07f, 2.0f);
        for (std::size_t i = 0; i < activeAlternateMoves.size(); ++i)
        {
            const std::optional<ChessBoardSuggestedSquares> alternate = ChessBoardComputeSuggestedSquares(activeAlternateMoves[i], blackAtBottom, boardOrigin, squareSize);
            if (!alternate)
                continue;

            const ImU32 color = kChessBoardAlternateArrowColors[std::min(i, std::size(kChessBoardAlternateArrowColors) - 1)];
            ChessBoardDrawArrow(drawList, alternate->FromCenter, alternate->ToCenter, color, alternateThickness);
        }
    }

    // Lookahead arrow drawn before the primary one so the primary arrow still reads as most
    // prominent - the anticipated reply to the primary suggestion, meaningful in both the live
    // and sandbox positions. Gated behind m_ShowLookaheadArrow.
    if (m_ShowLookaheadArrow)
    {
        const std::optional<std::string> activeLookaheadMove = sandboxActive ? m_Sandbox->GetLookaheadMove() : lookaheadMove;
        const std::optional<ChessBoardSuggestedSquares> lookahead = ChessBoardComputeSuggestedSquares(activeLookaheadMove, blackAtBottom, boardOrigin, squareSize);
        if (lookahead)
            ChessBoardDrawArrow(drawList, lookahead->FromCenter, lookahead->ToCenter, kChessBoardLookaheadArrowColor, std::max(squareSize * 0.08f, 2.5f));
    }

    // Primary arrow drawn last (among arrows) so it sits on top of the pieces/lookahead arrow.
    // Red instead of the default orange when mateInfo is set, since primarySuggestedMove and
    // activeEnginePanel's search info come from the same ongoing search stream.
    if (suggested)
        ChessBoardDrawArrow(drawList, suggested->FromCenter, suggested->ToCenter, mateInfo ? kChessBoardMatingArrowColor : kChessBoardArrowColor, std::max(squareSize * 0.1f, 3.0f));

    // User-drawn planning arrows - drawn on top of the engine's own arrows since they're the
    // player's own active annotations.
    m_Widget.DrawAnnotationArrows(drawList, blackAtBottom, boardOrigin, squareSize, squareUnderMouse);

    // On-board "mate in N" banner, top-left corner of the board itself rather than buried in
    // the score text below - drawn last so it sits above the board/pieces/arrow.
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

    // Reserves layout space for the eval bar + board so the window sizes/scrolls correctly -
    // everything above was drawn directly via the draw list, not through any layout-owning
    // widget.
    ImGui::Dummy(ImVec2(kChessBoardEvalBarWidth + kChessBoardEvalBarGap + boardSize, boardSize));

    // Promotion picker popup - positioned over the destination square, 4 piece-texture buttons.
    // Opened by ChessBoardWidget::UpdateInteraction() when a drop matched more than one legal move.
    m_Widget.DrawPromotionPopup(*m_Sandbox, *m_Textures, blackAtBottom, boardOrigin, squareSize);

    if (accuracyPercent)
        ImGui::Text("Accuracy: %.1f%%", *accuracyPercent);
    else
        ImGui::TextDisabled("Accuracy: -");

    ImGui::Separator();
    activeEnginePanel.DrawContents();

    ImGui::End();
}
