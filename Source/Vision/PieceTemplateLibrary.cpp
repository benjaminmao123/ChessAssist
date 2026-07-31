#include "PieceTemplateLibrary.h"

#include <opencv2/imgproc.hpp>

#include <limits>

namespace
{
constexpr int kDiffThreshold = 40;
constexpr double kMinForegroundRatio = 0.04;  // fraction of cell area that must differ from background to count as "occupied"
constexpr double kMaxNormalizedDiff = 0.35;   // TM_SQDIFF_NORMED: 0 = identical, 1 = maximally different

cv::Rect LargestContourBoundingBox(const cv::Mat& mask)
{
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    cv::Rect best;
    double bestArea = 0.0;

    for (const std::vector<cv::Point>& contour : contours)
    {
        const double area = cv::contourArea(contour);
        if (area > bestArea)
        {
            bestArea = area;
            best = cv::boundingRect(contour);
        }
    }

    return best;
}
}  // namespace

void PieceTemplateLibrary::BootstrapFromStartingPosition(const std::array<cv::Mat, 64>& cells)
{
    m_Templates.clear();

    // Ranks 3-6 (index 2..5) are guaranteed empty in the starting position, giving a
    // ground-truth sample of each square color's background.
    cv::Scalar lightSum(0, 0, 0);
    cv::Scalar darkSum(0, 0, 0);
    int lightCount = 0;
    int darkCount = 0;

    for (int rank = 2; rank <= 5; ++rank)
    {
        for (int file = 0; file < 8; ++file)
        {
            const cv::Mat& cell = cells[SquareIndex(file, rank)];
            if (cell.empty())
                continue;

            const cv::Scalar mean = cv::mean(cell);

            if (IsLightSquare(file, rank))
            {
                lightSum += mean;
                ++lightCount;
            }
            else
            {
                darkSum += mean;
                ++darkCount;
            }
        }
    }

    if (lightCount == 0 || darkCount == 0)
        return;

    const cv::Scalar lightAvg = lightSum / lightCount;
    const cv::Scalar darkAvg = darkSum / darkCount;

    m_LightBackground = cv::Vec3b(cv::saturate_cast<uchar>(lightAvg[0]), cv::saturate_cast<uchar>(lightAvg[1]), cv::saturate_cast<uchar>(lightAvg[2]));
    m_DarkBackground = cv::Vec3b(cv::saturate_cast<uchar>(darkAvg[0]), cv::saturate_cast<uchar>(darkAvg[1]), cv::saturate_cast<uchar>(darkAvg[2]));

    for (int rank = 0; rank < 8; ++rank)
    {
        for (int file = 0; file < 8; ++file)
        {
            const std::optional<Piece> startingPiece = GetStandardStartingPiece(file, rank);
            if (!startingPiece)
                continue;

            const cv::Mat& cell = cells[SquareIndex(file, rank)];
            if (cell.empty())
                continue;

            const bool light = IsLightSquare(file, rank);
            const cv::Mat mask = BuildForegroundMask(cell, light);
            const cv::Rect box = LargestContourBoundingBox(mask);

            if (box.width <= 0 || box.height <= 0)
                continue;

            Template newTemplate;
            newTemplate.PieceValue = *startingPiece;
            newTemplate.Image = cell(box).clone();
            newTemplate.Mask = mask(box).clone();

            m_Templates.push_back(std::move(newTemplate));
        }
    }
}

bool PieceTemplateLibrary::IsBootstrapped() const
{
    return !m_Templates.empty();
}

cv::Mat PieceTemplateLibrary::BuildForegroundMask(const cv::Mat& cell, bool isLightSquare) const
{
    const cv::Vec3b& reference = isLightSquare ? m_LightBackground : m_DarkBackground;

    cv::Mat referenceImage(cell.size(), cell.type(), cv::Scalar(reference[0], reference[1], reference[2]));

    cv::Mat diff;
    cv::absdiff(cell, referenceImage, diff);

    cv::Mat gray;
    cv::cvtColor(diff, gray, cv::COLOR_BGR2GRAY);

    cv::Mat mask;
    cv::threshold(gray, mask, kDiffThreshold, 255, cv::THRESH_BINARY);

    const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);

    return mask;
}

std::optional<Piece> PieceTemplateLibrary::Classify(const cv::Mat& cell, bool isLightSquare) const
{
    if (cell.empty() || m_Templates.empty())
        return std::nullopt;

    const cv::Mat mask = BuildForegroundMask(cell, isLightSquare);

    const double foregroundRatio = static_cast<double>(cv::countNonZero(mask)) / static_cast<double>(mask.total());
    if (foregroundRatio < kMinForegroundRatio)
        return std::nullopt;  // empty square

    const cv::Rect box = LargestContourBoundingBox(mask);
    if (box.width <= 0 || box.height <= 0)
        return std::nullopt;

    const cv::Mat candidate = cell(box);
    const cv::Mat candidateMask = mask(box);

    double bestDiff = std::numeric_limits<double>::max();
    std::optional<Piece> bestPiece;

    for (const Template& candidateTemplate : m_Templates)
    {
        cv::Mat resizedCandidate;
        cv::Mat resizedMask;
        cv::resize(candidate, resizedCandidate, candidateTemplate.Image.size());
        cv::resize(candidateMask, resizedMask, candidateTemplate.Image.size(), 0, 0, cv::INTER_NEAREST);

        cv::Mat combinedMask;
        cv::bitwise_and(resizedMask, candidateTemplate.Mask, combinedMask);

        if (cv::countNonZero(combinedMask) < 4)
            continue;  // not enough overlap to produce a meaningful score

        cv::Mat result;
        cv::matchTemplate(resizedCandidate, candidateTemplate.Image, result, cv::TM_SQDIFF_NORMED, combinedMask);

        const double diff = result.at<float>(0, 0);
        if (diff < bestDiff)
        {
            bestDiff = diff;
            bestPiece = candidateTemplate.PieceValue;
        }
    }

    if (bestDiff > kMaxNormalizedDiff)
        return std::nullopt;

    return bestPiece;
}
