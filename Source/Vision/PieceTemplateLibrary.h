#pragma once

#include "VisionTypes.h"

#include <opencv2/core.hpp>

#include <array>
#include <filesystem>
#include <optional>
#include <vector>

class PieceTemplateLibrary
{
public:
    // Bootstraps the 12-class template gallery from 64 cells known to be in the standard
    // chess starting position (see GetStandardStartingPiece).
    void BootstrapFromStartingPosition(const std::array<cv::Mat, 64>& cells);

    // Bootstraps from a folder of pre-supplied reference images instead of a live capture:
    // wP.png/bP.png/.../wK.png/bK.png (RGBA piece sprites, one per class, transparent
    // background). Independent of any particular game's position - unlike
    // BootstrapFromStartingPosition, this doesn't require the board to currently show the
    // standard starting layout, which is what makes mid-game calibration possible.
    [[nodiscard]] bool BootstrapFromReferenceAssets(const std::filesystem::path& assetsDirectory);

    [[nodiscard]] bool IsBootstrapped() const;

    // Empty/occupied gate plus 12-way classification for a single cell. expectedBackground
    // is this cell's square-color (light/dark) "clean" background, ordinarily aggregated
    // board-wide by the caller (see BoardStateExtractor) - used to correct for a translucent
    // highlight tint on this specific cell (background AND any piece on it) before comparing
    // its colors against the (untinted) stored templates.
    [[nodiscard]] std::optional<Piece> Classify(const cv::Mat& cell, const cv::Vec3b& expectedBackground) const;

private:
    struct Template
    {
        Piece PieceValue;
        cv::Mat Image;
        cv::Mat Mask;
    };

    [[nodiscard]] cv::Mat BuildForegroundMask(const cv::Mat& cell) const;

    std::vector<Template> m_Templates;
};
