#include "BoardStatePanel.h"

#include <imgui.h>

namespace
{
const char* PieceLabel(const std::optional<Piece>& piece)
{
    if (!piece)
        return "..";

    static constexpr const char* kWhiteLabels[] = {"wP", "wN", "wB", "wR", "wQ", "wK"};
    static constexpr const char* kBlackLabels[] = {"bP", "bN", "bB", "bR", "bQ", "bK"};

    const char* const* labels = (piece->Color == PieceColor::White) ? kWhiteLabels : kBlackLabels;
    return labels[static_cast<int>(piece->Type)];
}
}  // namespace

void BoardStatePanel::Draw(const BoardState& board)
{
    ImGui::Begin("Tracked Board");

    if (ImGui::BeginTable("BoardStateGrid", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
    {
        for (int rank = 7; rank >= 0; --rank)
        {
            ImGui::TableNextRow();
            for (int file = 0; file < 8; ++file)
            {
                ImGui::TableSetColumnIndex(file);
                ImGui::TextUnformatted(PieceLabel(board[SquareIndex(file, rank)]));
            }
        }

        ImGui::EndTable();
    }

    ImGui::End();
}
