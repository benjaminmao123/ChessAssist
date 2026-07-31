#pragma once

#include "VisionTypes.h"

#include <opencv2/core.hpp>

#include <array>
#include <optional>
#include <vector>

class PieceTemplateLibrary
{
public:
    // Bootstraps the 12-class template gallery from 64 cells known to be in the standard
    // chess starting position (see GetStandardStartingPiece).
    void BootstrapFromStartingPosition(const std::array<cv::Mat, 64>& cells);

    [[nodiscard]] bool IsBootstrapped() const;

    // Empty/occupied gate plus 12-way classification for a single cell. isLightSquare must
    // match the square this cell was cropped from (see VisionTypes.h's IsLightSquare).
    [[nodiscard]] std::optional<Piece> Classify(const cv::Mat& cell, bool isLightSquare) const;

private:
    struct Template
    {
        Piece PieceValue;
        cv::Mat Image;
        cv::Mat Mask;
    };

    [[nodiscard]] cv::Mat BuildForegroundMask(const cv::Mat& cell, bool isLightSquare) const;

    cv::Vec3b m_LightBackground{};
    cv::Vec3b m_DarkBackground{};
    std::vector<Template> m_Templates;
};
