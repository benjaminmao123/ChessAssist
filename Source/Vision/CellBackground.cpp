#include "CellBackground.h"

#include <algorithm>

namespace
{
uchar MedianByte(std::vector<uchar>& values)
{
    std::nth_element(values.begin(), values.begin() + values.size() / 2, values.end());
    return values[values.size() / 2];
}
}  // namespace

namespace CellBackground
{
cv::Vec3b EstimateFromWholeCell(const cv::Mat& cell)
{
    std::vector<uchar> blue, green, red;
    blue.reserve(cell.total());
    green.reserve(cell.total());
    red.reserve(cell.total());

    for (int y = 0; y < cell.rows; ++y)
    {
        const cv::Vec3b* row = cell.ptr<cv::Vec3b>(y);
        for (int x = 0; x < cell.cols; ++x)
        {
            blue.push_back(row[x][0]);
            green.push_back(row[x][1]);
            red.push_back(row[x][2]);
        }
    }

    return cv::Vec3b(MedianByte(blue), MedianByte(green), MedianByte(red));
}

cv::Vec3b Median(const std::vector<cv::Vec3b>& colors)
{
    if (colors.empty())
        return cv::Vec3b(0, 0, 0);

    std::vector<uchar> blue, green, red;
    blue.reserve(colors.size());
    green.reserve(colors.size());
    red.reserve(colors.size());

    for (const cv::Vec3b& color : colors)
    {
        blue.push_back(color[0]);
        green.push_back(color[1]);
        red.push_back(color[2]);
    }

    return cv::Vec3b(MedianByte(blue), MedianByte(green), MedianByte(red));
}
}  // namespace CellBackground
