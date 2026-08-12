#include "ChessBoardWidget.h"

#include <algorithm>
#include <cmath>

ImVec2 ChessBoardSquareMin(int file, int rank, bool blackAtBottom, ImVec2 boardOrigin, float squareSize)
{
    const int visualCol = blackAtBottom ? (7 - file) : file;
    const int visualRow = blackAtBottom ? rank : (7 - rank);
    return ImVec2(boardOrigin.x + static_cast<float>(visualCol) * squareSize, boardOrigin.y + static_cast<float>(visualRow) * squareSize);
}

std::optional<int> ChessBoardSquareAtScreenPos(ImVec2 pos, ImVec2 boardOrigin, float squareSize, bool blackAtBottom)
{
    const float relX = pos.x - boardOrigin.x;
    const float relY = pos.y - boardOrigin.y;
    const float boardSize = squareSize * 8.0f;
    if (relX < 0.0f || relY < 0.0f || relX >= boardSize || relY >= boardSize)
        return std::nullopt;

    const int visualCol = static_cast<int>(relX / squareSize);
    const int visualRow = static_cast<int>(relY / squareSize);

    const int file = blackAtBottom ? (7 - visualCol) : visualCol;
    const int rank = blackAtBottom ? visualRow : (7 - visualRow);
    return SquareIndex(file, rank);
}

ImVec2 ChessBoardSquareCenter(int square, bool blackAtBottom, ImVec2 boardOrigin, float squareSize)
{
    const ImVec2 min = ChessBoardSquareMin(square % 8, square / 8, blackAtBottom, boardOrigin, squareSize);
    return ImVec2(min.x + squareSize * 0.5f, min.y + squareSize * 0.5f);
}

std::optional<ChessBoardSuggestedSquares> ChessBoardComputeSuggestedSquares(const std::optional<std::string>& suggestedMove, bool blackAtBottom, ImVec2 boardOrigin, float squareSize)
{
    if (!suggestedMove || suggestedMove->size() < 4)
        return std::nullopt;

    const std::string& uci = *suggestedMove;
    const int fromFile = uci[0] - 'a';
    const int fromRank = uci[1] - '1';
    const int toFile = uci[2] - 'a';
    const int toRank = uci[3] - '1';

    ChessBoardSuggestedSquares squares;
    squares.FromMin = ChessBoardSquareMin(fromFile, fromRank, blackAtBottom, boardOrigin, squareSize);
    squares.FromMax = ImVec2(squares.FromMin.x + squareSize, squares.FromMin.y + squareSize);
    squares.FromCenter = ImVec2(squares.FromMin.x + squareSize * 0.5f, squares.FromMin.y + squareSize * 0.5f);

    squares.ToMin = ChessBoardSquareMin(toFile, toRank, blackAtBottom, boardOrigin, squareSize);
    squares.ToMax = ImVec2(squares.ToMin.x + squareSize, squares.ToMin.y + squareSize);
    squares.ToCenter = ImVec2(squares.ToMin.x + squareSize * 0.5f, squares.ToMin.y + squareSize * 0.5f);

    return squares;
}

void ChessBoardDrawArrow(ImDrawList* drawList, ImVec2 from, ImVec2 to, ImU32 color, float thickness)
{
    drawList->AddLine(from, to, color, thickness);

    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < 1.0f)
        return;

    const float unitX = dx / length;
    const float unitY = dy / length;
    const float perpX = -unitY;
    const float perpY = unitX;

    constexpr float kHeadLength = 16.0f;
    constexpr float kHeadWidth = 10.0f;

    const ImVec2 baseCenter(to.x - unitX * kHeadLength, to.y - unitY * kHeadLength);
    const ImVec2 left(baseCenter.x + perpX * kHeadWidth, baseCenter.y + perpY * kHeadWidth);
    const ImVec2 right(baseCenter.x - perpX * kHeadWidth, baseCenter.y - perpY * kHeadWidth);

    drawList->AddTriangleFilled(to, left, right, color);
}

void ChessBoardDrawEvalBar(ImDrawList* drawList, ImVec2 barMin, ImVec2 barMax, std::optional<float> whiteFraction, bool blackAtBottom)
{
    constexpr ImU32 kBlackFillColor = IM_COL32(0x28, 0x28, 0x28, 255);
    constexpr ImU32 kWhiteFillColor = IM_COL32(0xEB, 0xEB, 0xEB, 255);
    constexpr ImU32 kMidlineColor = IM_COL32(0x78, 0x78, 0x78, 180);
    constexpr ImU32 kBorderColor = IM_COL32(0, 0, 0, 255);

    const float whiteFrac = whiteFraction.value_or(0.5f);
    const float bottomFraction = blackAtBottom ? (1.0f - whiteFrac) : whiteFrac;
    const ImU32 bottomColor = blackAtBottom ? kBlackFillColor : kWhiteFillColor;
    const ImU32 topColor = blackAtBottom ? kWhiteFillColor : kBlackFillColor;

    const float barHeight = barMax.y - barMin.y;
    const float bottomHeight = barHeight * bottomFraction;

    drawList->AddRectFilled(barMin, ImVec2(barMax.x, barMax.y - bottomHeight), topColor);
    drawList->AddRectFilled(ImVec2(barMin.x, barMax.y - bottomHeight), barMax, bottomColor);

    const float midY = barMin.y + barHeight * 0.5f;
    drawList->AddLine(ImVec2(barMin.x, midY), ImVec2(barMax.x, midY), kMidlineColor, 1.5f);

    drawList->AddRect(barMin, barMax, kBorderColor);
}

namespace
{
// File letters (a-h) in the bottom-right corner of the bottom visual row's squares, rank
// numbers (1-8) in the top-left corner of the left visual column's squares - lichess/chess.com
// convention, drawn inside the board's own squares so no extra layout space is needed. Colored
// to contrast with each square's own color.
void DrawCoordinateLabels(ImDrawList* drawList, ImVec2 boardOrigin, float squareSize, bool blackAtBottom)
{
    constexpr float kLabelPadding = 2.0f;

    const int bottomRank = blackAtBottom ? 7 : 0;
    for (int file = 0; file < 8; ++file)
    {
        const ImVec2 min = ChessBoardSquareMin(file, bottomRank, blackAtBottom, boardOrigin, squareSize);
        const char label[2] = {static_cast<char>('a' + file), '\0'};
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        const ImVec2 pos(min.x + squareSize - textSize.x - kLabelPadding, min.y + squareSize - textSize.y - kLabelPadding);
        drawList->AddText(pos, IsLightSquare(file, bottomRank) ? kChessBoardDarkSquareColor : kChessBoardLightSquareColor, label);
    }

    const int leftFile = blackAtBottom ? 7 : 0;
    for (int rank = 0; rank < 8; ++rank)
    {
        const ImVec2 min = ChessBoardSquareMin(leftFile, rank, blackAtBottom, boardOrigin, squareSize);
        const char label[2] = {static_cast<char>('1' + rank), '\0'};
        const ImVec2 pos(min.x + kLabelPadding, min.y + kLabelPadding);
        drawList->AddText(pos, IsLightSquare(leftFile, rank) ? kChessBoardDarkSquareColor : kChessBoardLightSquareColor, label);
    }
}
}  // namespace

void ChessBoardWidget::UpdateInteraction(IPlayableBoard& board, std::optional<int> squareUnderMouse, bool windowHovered)
{
    DragState& drag = m_Drag;

    const auto isLegalDestination = [&](int square) { return std::any_of(drag.LegalDestinations.begin(), drag.LegalDestinations.end(), [square](const MoveGenerator::LegalMove& m) { return m.To == square; }); };

    const auto resolveDrop = [&](int destSquare)
    {
        std::vector<MoveGenerator::LegalMove> matches;
        for (const MoveGenerator::LegalMove& move : drag.LegalDestinations)
        {
            if (move.To == destSquare)
                matches.push_back(move);
        }

        drag = DragState{};

        if (matches.empty())
            return;  // illegal drop - snap back (rendering just resumes from board.GetBoard())

        if (matches.size() == 1)
        {
            board.PlayMove(matches.front());
            return;
        }

        m_PendingPromotionChoices = std::move(matches);
        m_PendingPromotionSquare = destSquare;
        ImGui::OpenPopup("ChessBoardPromotionPicker");
    };

    // Left-clicking the board clears any drawn planning arrows - chess.com's own convention
    // (left-click dismisses annotations; only right-click-drag creates/removes them). Fires
    // regardless of what else this click does (pick up a piece, play a move, deselect) -
    // matches "clicking to interact with the board" broadly, not just empty squares.
    if (squareUnderMouse && windowHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        m_AnnotationArrows.clear();

    if (drag.CurrentMode != DragState::Mode::Dragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && squareUnderMouse && (drag.CurrentMode != DragState::Mode::Idle || windowHovered))
    {
        const int square = *squareUnderMouse;
        const std::optional<Piece>& clickedPiece = board.GetBoard()[square];

        if (drag.CurrentMode == DragState::Mode::PickedUp && isLegalDestination(square))
        {
            resolveDrop(square);
        }
        else if (clickedPiece && clickedPiece->Color == board.GetSideToMove())
        {
            std::vector<MoveGenerator::LegalMove> legalMoves = board.GetLegalMovesFrom(square);
            if (!legalMoves.empty())
            {
                drag.CurrentMode = DragState::Mode::PickedUp;
                drag.PickedSquare = square;
                drag.LegalDestinations = std::move(legalMoves);
            }
            else
            {
                drag = DragState{};
            }
        }
        else
        {
            drag = DragState{};
        }
    }

    if (drag.CurrentMode == DragState::Mode::PickedUp && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f))
        drag.CurrentMode = DragState::Mode::Dragging;

    if (drag.CurrentMode == DragState::Mode::Dragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        if (squareUnderMouse)
            resolveDrop(*squareUnderMouse);
        else
            drag = DragState{};
    }

    // Right-click-drag draws/toggles a planning arrow (see DrawAnnotationArrows()); a plain
    // right-click tap (no drag - released on the same square it started on) instead falls back
    // to its previous job of deselecting an in-progress pick-up, so that behavior isn't lost.
    AnnotationDragState& annotationDrag = m_AnnotationDrag;

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && squareUnderMouse && windowHovered)
    {
        annotationDrag.Active = true;
        annotationDrag.StartSquare = *squareUnderMouse;
    }

    if (annotationDrag.Active && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
    {
        annotationDrag.Active = false;

        if (squareUnderMouse && *squareUnderMouse != annotationDrag.StartSquare)
        {
            const std::pair<int, int> arrow{annotationDrag.StartSquare, *squareUnderMouse};
            std::vector<std::pair<int, int>>& arrows = m_AnnotationArrows;
            const auto it = std::find(arrows.begin(), arrows.end(), arrow);
            if (it != arrows.end())
                arrows.erase(it);  // dragging the same arrow again removes it
            else
                arrows.push_back(arrow);
        }
        else if (drag.CurrentMode != DragState::Mode::Idle)
        {
            drag = DragState{};
        }
    }
}

void ChessBoardWidget::DrawBoardAndPieces(ImDrawList* drawList, const ChessPieceTextures& textures, const BoardState& board, bool blackAtBottom, ImVec2 boardOrigin, ImVec2 boardEnd, float squareSize, ImVec2 mousePos) const
{
    if (textures.BoardTexture() != 0)
    {
        drawList->AddImage(static_cast<ImTextureID>(textures.BoardTexture()), boardOrigin, boardEnd);
    }
    else
    {
        // Board texture missing/failed to load - draw a plain checkerboard instead so the
        // panel still shows something usable rather than a blank window.
        for (int rank = 0; rank < 8; ++rank)
        {
            for (int file = 0; file < 8; ++file)
            {
                const ImVec2 squareMin = ChessBoardSquareMin(file, rank, blackAtBottom, boardOrigin, squareSize);
                const ImVec2 squareMax(squareMin.x + squareSize, squareMin.y + squareSize);
                drawList->AddRectFilled(squareMin, squareMax, IsLightSquare(file, rank) ? kChessBoardLightSquareColor : kChessBoardDarkSquareColor);
            }
        }
    }

    DrawCoordinateLabels(drawList, boardOrigin, squareSize, blackAtBottom);

    for (int visualRow = 0; visualRow < 8; ++visualRow)
    {
        for (int visualCol = 0; visualCol < 8; ++visualCol)
        {
            const int file = blackAtBottom ? (7 - visualCol) : visualCol;
            const int rank = blackAtBottom ? visualRow : (7 - visualRow);
            const int square = SquareIndex(file, rank);

            // Skip the piece currently being dragged here - redrawn below, centered on the
            // cursor, so it renders on top of everything else instead of under the highlights.
            if (m_Drag.CurrentMode == DragState::Mode::Dragging && square == m_Drag.PickedSquare)
                continue;

            const std::optional<Piece>& piece = board[square];
            if (!piece)
                continue;

            const unsigned int texture = textures.TextureFor(*piece);
            if (texture == 0)
                continue;

            const ImVec2 squareMin(boardOrigin.x + static_cast<float>(visualCol) * squareSize, boardOrigin.y + static_cast<float>(visualRow) * squareSize);
            const ImVec2 squareMax(squareMin.x + squareSize, squareMin.y + squareSize);
            drawList->AddImage(static_cast<ImTextureID>(texture), squareMin, squareMax);
        }
    }

    if (m_Drag.CurrentMode == DragState::Mode::Dragging)
    {
        const std::optional<Piece>& draggedPiece = board[m_Drag.PickedSquare];
        const unsigned int texture = draggedPiece ? textures.TextureFor(*draggedPiece) : 0;
        if (texture != 0)
        {
            const ImVec2 half(squareSize * 0.5f, squareSize * 0.5f);
            drawList->AddImage(static_cast<ImTextureID>(texture), ImVec2(mousePos.x - half.x, mousePos.y - half.y), ImVec2(mousePos.x + half.x, mousePos.y + half.y));
        }
    }
}

void ChessBoardWidget::DrawDragHighlights(ImDrawList* drawList, const BoardState& board, bool blackAtBottom, ImVec2 boardOrigin, float squareSize) const
{
    if (m_Drag.CurrentMode == DragState::Mode::Idle)
        return;

    const ImVec2 pickedMin = ChessBoardSquareMin(m_Drag.PickedSquare % 8, m_Drag.PickedSquare / 8, blackAtBottom, boardOrigin, squareSize);
    drawList->AddRectFilled(pickedMin, ImVec2(pickedMin.x + squareSize, pickedMin.y + squareSize), kChessBoardPickedSquareColor);

    // Lichess-style destination markers: a small filled dot for a quiet move, a ring hugging
    // the inside edge of the square for a capture - including en passant, whose destination
    // square is itself empty, so board[move.To] alone wouldn't catch it.
    for (const MoveGenerator::LegalMove& move : m_Drag.LegalDestinations)
    {
        const ImVec2 center = ChessBoardSquareCenter(move.To, blackAtBottom, boardOrigin, squareSize);
        const bool isCapture = move.IsEnPassant || board[move.To].has_value();
        if (isCapture)
            drawList->AddCircle(center, squareSize * 0.46f, kChessBoardLegalDestinationColor, 0, squareSize * 0.07f);
        else
            drawList->AddCircleFilled(center, squareSize * 0.16f, kChessBoardLegalDestinationColor);
    }
}

void ChessBoardWidget::DrawAnnotationArrows(ImDrawList* drawList, bool blackAtBottom, ImVec2 boardOrigin, float squareSize, std::optional<int> squareUnderMouse) const
{
    const float annotationThickness = std::max(squareSize * 0.09f, 2.5f);
    for (const auto& [fromSquare, toSquare] : m_AnnotationArrows)
        ChessBoardDrawArrow(drawList, ChessBoardSquareCenter(fromSquare, blackAtBottom, boardOrigin, squareSize), ChessBoardSquareCenter(toSquare, blackAtBottom, boardOrigin, squareSize), kChessBoardAnnotationArrowColor,
                             annotationThickness);

    // Live preview of the arrow currently being drawn (right button still held, dragged past
    // its start square) - not yet committed to m_AnnotationArrows until release.
    if (m_AnnotationDrag.Active && squareUnderMouse && *squareUnderMouse != m_AnnotationDrag.StartSquare)
    {
        ChessBoardDrawArrow(drawList, ChessBoardSquareCenter(m_AnnotationDrag.StartSquare, blackAtBottom, boardOrigin, squareSize), ChessBoardSquareCenter(*squareUnderMouse, blackAtBottom, boardOrigin, squareSize),
                             kChessBoardAnnotationArrowColor, annotationThickness);
    }
}

void ChessBoardWidget::DrawPromotionPopup(IPlayableBoard& board, const ChessPieceTextures& textures, bool blackAtBottom, ImVec2 boardOrigin, float squareSize)
{
    if (!m_PendingPromotionChoices)
        return;

    const int destFile = m_PendingPromotionSquare % 8;
    const int destRank = m_PendingPromotionSquare / 8;
    const ImVec2 popupPos = ChessBoardSquareMin(destFile, destRank, blackAtBottom, boardOrigin, squareSize);
    ImGui::SetNextWindowPos(popupPos);

    if (ImGui::BeginPopup("ChessBoardPromotionPicker"))
    {
        // Copied out (not iterated in place) since a clicked choice mutates/resets
        // m_PendingPromotionChoices below - iterating the optional's own vector while resetting
        // it mid-loop would be undefined behavior. The board hasn't been mutated by PlayMove
        // yet at this point, so choices.front().From (the pawn's square) still reflects who's
        // promoting.
        const std::vector<MoveGenerator::LegalMove> choices = *m_PendingPromotionChoices;
        const BoardState& boardState = board.GetBoard();
        const PieceColor promotingColor = (!choices.empty() && boardState[choices.front().From]) ? boardState[choices.front().From]->Color : board.GetSideToMove();

        std::optional<MoveGenerator::LegalMove> chosen;
        for (const MoveGenerator::LegalMove& choice : choices)
        {
            ImGui::PushID(static_cast<int>(*choice.Promotion));
            const unsigned int texture = textures.TextureFor(Piece{*choice.Promotion, promotingColor});
            const bool clicked = (texture != 0) ? ImGui::ImageButton("##promo", static_cast<ImTextureID>(texture), ImVec2(squareSize, squareSize)) : ImGui::Button("?", ImVec2(squareSize, squareSize));
            if (clicked)
                chosen = choice;
            ImGui::PopID();
            ImGui::SameLine();
        }

        if (chosen)
        {
            board.PlayMove(*chosen);
            m_PendingPromotionChoices.reset();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
    else
    {
        // Closed without a selection (e.g. clicked away) - drop the pending state so it
        // doesn't linger and re-suppress interaction forever.
        m_PendingPromotionChoices.reset();
    }
}

void ChessBoardWidget::ClearAnnotationsIfBoardChanged(const BoardState& board)
{
    if (m_LastSeenBoard && *m_LastSeenBoard != board)
        m_AnnotationArrows.clear();
    m_LastSeenBoard = board;
}
