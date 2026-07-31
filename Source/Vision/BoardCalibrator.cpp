#include "BoardCalibrator.h"

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <vector>

namespace
{
struct ClickState
{
    std::vector<cv::Point> Points;
};

void OnMouse(int event, int x, int y, int, void* userData)
{
    if (event != cv::EVENT_LBUTTONDOWN)
        return;

    auto* state = static_cast<ClickState*>(userData);
    state->Points.emplace_back(x, y);
}
}  // namespace

namespace BoardCalibrator
{
std::optional<BoardRegion> CalibrateInteractive(const cv::Mat& frame, BoardOrientation orientation)
{
    const std::string windowName = "ChessAssist Calibration - click top-left then bottom-right corner of the board, Esc to cancel";

    ClickState state;

    cv::namedWindow(windowName, cv::WINDOW_AUTOSIZE);
    cv::setMouseCallback(windowName, OnMouse, &state);

    std::optional<BoardRegion> result;

    while (true)
    {
        cv::Mat display = frame.clone();
        for (const cv::Point& point : state.Points)
            cv::circle(display, point, 5, cv::Scalar(0, 0, 255), cv::FILLED);

        cv::imshow(windowName, display);
        const int key = cv::waitKey(30);

        if (key == 27)  // Esc
            break;

        if (state.Points.size() >= 2)
        {
            const cv::Rect rect = cv::boundingRect(state.Points);
            result = BoardRegion{rect, orientation};
            break;
        }
    }

    cv::destroyWindow(windowName);
    return result;
}

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
