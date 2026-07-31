#pragma once

#include "../Engine/EngineController.h"
#include "../Vision/PieceTemplateLibrary.h"
#include "../Vision/VisionTypes.h"
#include "GameTracker.h"

#include <opencv2/core.hpp>

#include <filesystem>
#include <optional>
#include <string>

// Orchestrates the live capture -> recognize -> detect-move -> engine pipeline for one
// game: owns the bootstrapped PieceTemplateLibrary and GameTracker for the game currently
// being watched, and drives EngineController (owned by the caller) whenever a new move is
// recognized.
class GameSession
{
public:
    explicit GameSession(EngineController& controller);

    // Loads piece-recognition templates from a reference asset folder (see
    // PieceTemplateLibrary::BootstrapFromReferenceAssets). One-time setup, independent of
    // any particular game - call once before the first StartNewGame().
    [[nodiscard]] bool LoadPieceTemplates(const std::filesystem::path& assetsDirectory);

    [[nodiscard]] bool AreTemplatesLoaded() const;

    // Marks region as the board being watched and takes frame as the move-detection
    // baseline, whatever position it currently shows. Requires LoadPieceTemplates() to have
    // already succeeded. Resets any previously tracked game.
    [[nodiscard]] bool StartNewGame(const cv::Mat& frame, const BoardRegion& region);

    [[nodiscard]] bool IsActive() const;
    [[nodiscard]] const BoardRegion& GetRegion() const;
    [[nodiscard]] const GameTracker& GetTracker() const;

    // Call periodically with a freshly captured frame covering GetRegion(). Returns the
    // detected move (UCI notation) if one was found and recorded, else nullopt.
    std::optional<std::string> Poll(const cv::Mat& frame);

private:
    void RequestEngineMove();

    EngineController* m_Controller = nullptr;
    PieceTemplateLibrary m_TemplateLibrary;
    GameTracker m_Tracker;
    BoardRegion m_Region;
    bool m_Active = false;
};
