#include "BoardCalibrator.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>

namespace BoardCalibrator
{
bool LooksLikeBoard(const cv::Mat& frame, const cv::Rect& region)
{
    const cv::Rect bounds = region & cv::Rect(0, 0, frame.cols, frame.rows);
    if (bounds.width <= 0 || bounds.height <= 0)
        return false;

    cv::Mat cropped = frame(bounds);

    cv::Mat thumbnail;
    cv::resize(cropped, thumbnail, cv::Size(8, 8), 0, 0, cv::INTER_AREA);

    cv::Mat samples(64, 3, CV_32F);
    for (int row = 0; row < 8; ++row)
    {
        for (int col = 0; col < 8; ++col)
        {
            const cv::Vec3b& pixel = thumbnail.at<cv::Vec3b>(row, col);
            float* sample = samples.ptr<float>(row * 8 + col);
            sample[0] = static_cast<float>(pixel[0]);
            sample[1] = static_cast<float>(pixel[1]);
            sample[2] = static_cast<float>(pixel[2]);
        }
    }

    cv::Mat labels;
    cv::Mat centers;
    cv::kmeans(samples, 2, labels, cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 10, 1.0), 3, cv::KMEANS_PP_CENTERS, centers);

    // A real checkerboard alternates cluster membership with (row + col) parity - count
    // how often that alternation actually holds (tolerant of piece-occluded cells).
    int matchingCells = 0;
    for (int row = 0; row < 8; ++row)
    {
        for (int col = 0; col < 8; ++col)
        {
            const int expectedParity = (row + col) % 2;
            const int clusterLabel = labels.at<int>(row * 8 + col);

            if (clusterLabel == expectedParity)
                ++matchingCells;
        }
    }

    // Cluster 0/1 labeling is arbitrary, so parity could line up inverted - take
    // whichever alignment is better.
    const int bestMatch = std::max(matchingCells, 64 - matchingCells);

    constexpr int kMinMatchingCells = 48;  // ~75% of 64
    return bestMatch >= kMinMatchingCells;
}
}  // namespace BoardCalibrator
