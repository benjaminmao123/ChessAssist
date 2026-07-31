#include "PieceTemplateLibrary.h"
#include "CellBackground.h"

#include <spdlog/spdlog.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <vector>

namespace
{
constexpr int kDiffThreshold = 40;
constexpr double kMinForegroundRatio = 0.04;  // fraction of cell area that must differ from background to count as "occupied"

// 0 = identical, 1 = maximally different - the accept/reject threshold for the blended
// color+shape diff computed in Classify().
constexpr double kMaxNormalizedDiff = 0.35;

// Every stored template and every candidate crop is resized to this fixed size exactly
// once (at bootstrap time / once per Classify() call respectively), rather than resizing
// the candidate against each template's own size during the match loop. With ~32 templates
// and up to 32 occupied cells, that resize-per-comparison approach was ~1000 cv::resize
// calls per board read - several seconds of pure per-call OpenCV overhead. Matching against
// a shared canonical size cuts that to one resize per cell.
constexpr int kCanonicalSize = 48;

struct ContourBox
{
    cv::Rect Box;
    double Area = 0.0;  // cv::contourArea of the contour Box was derived from, not Box's own area
};

ContourBox LargestContourBoundingBox(const cv::Mat& mask)
{
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    ContourBox best;

    for (const std::vector<cv::Point>& contour : contours)
    {
        const double area = cv::contourArea(contour);
        if (area > best.Area)
        {
            best.Area = area;
            best.Box = cv::boundingRect(contour);
        }
    }

    return best;
}

std::vector<cv::Point> LargestContour(const cv::Mat& mask)
{
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    std::vector<cv::Point> best;
    double bestArea = 0.0;

    for (std::vector<cv::Point>& contour : contours)
    {
        const double area = cv::contourArea(contour);
        if (area > bestArea)
        {
            bestArea = area;
            best = std::move(contour);
        }
    }

    return best;
}
}  // namespace

void PieceTemplateLibrary::BootstrapFromStartingPosition(const std::array<cv::Mat, 64>& cells)
{
    m_Templates.clear();

    const cv::Size canonicalSize(kCanonicalSize, kCanonicalSize);

    // The starting position gives up to 8 instances of a pawn per side, but masking
    // already makes a template background-agnostic (see BuildForegroundMask), so more than
    // a couple of instances per class doesn't meaningfully improve matching - it just adds
    // more comparisons to do on every board read. Cap it.
    constexpr int kMaxTemplatesPerClass = 2;
    int classCounts[12] = {};

    for (int rank = 0; rank < 8; ++rank)
    {
        for (int file = 0; file < 8; ++file)
        {
            const std::optional<Piece> startingPiece = GetStandardStartingPiece(file, rank);
            if (!startingPiece)
                continue;

            const int classIndex = static_cast<int>(startingPiece->Type) * 2 + static_cast<int>(startingPiece->Color);
            if (classCounts[classIndex] >= kMaxTemplatesPerClass)
                continue;

            const cv::Mat& cell = cells[SquareIndex(file, rank)];
            if (cell.empty())
                continue;

            const cv::Mat mask = BuildForegroundMask(cell);
            const cv::Rect box = LargestContourBoundingBox(mask).Box;

            if (box.width <= 0 || box.height <= 0)
                continue;

            Template newTemplate;
            newTemplate.PieceValue = *startingPiece;
            cv::resize(cell(box), newTemplate.Image, canonicalSize);
            cv::resize(mask(box), newTemplate.Mask, canonicalSize, 0, 0, cv::INTER_NEAREST);

            m_Templates.push_back(std::move(newTemplate));
            ++classCounts[classIndex];
        }
    }
}

bool PieceTemplateLibrary::BootstrapFromReferenceAssets(const std::filesystem::path& assetsDirectory)
{
    m_Templates.clear();

    const cv::Size canonicalSize(kCanonicalSize, kCanonicalSize);

    struct PieceFile
    {
        const char* Filename;
        Piece PieceValue;
    };

    const PieceFile pieceFiles[] = {
        {"wP.png", Piece{PieceType::Pawn, PieceColor::White}},
        {"bP.png", Piece{PieceType::Pawn, PieceColor::Black}},
        {"wN.png", Piece{PieceType::Knight, PieceColor::White}},
        {"bN.png", Piece{PieceType::Knight, PieceColor::Black}},
        {"wB.png", Piece{PieceType::Bishop, PieceColor::White}},
        {"bB.png", Piece{PieceType::Bishop, PieceColor::Black}},
        {"wR.png", Piece{PieceType::Rook, PieceColor::White}},
        {"bR.png", Piece{PieceType::Rook, PieceColor::Black}},
        {"wQ.png", Piece{PieceType::Queen, PieceColor::White}},
        {"bQ.png", Piece{PieceType::Queen, PieceColor::Black}},
        {"wK.png", Piece{PieceType::King, PieceColor::White}},
        {"bK.png", Piece{PieceType::King, PieceColor::Black}},
    };

    for (const PieceFile& pieceFile : pieceFiles)
    {
        const std::filesystem::path path = assetsDirectory / pieceFile.Filename;
        const cv::Mat source = cv::imread(path.string(), cv::IMREAD_UNCHANGED);

        if (source.empty() || source.channels() != 4)
        {
            spdlog::error("Could not read {} as an RGBA image (need a transparent-background PNG)", path.string());
            continue;
        }

        std::vector<cv::Mat> channels;
        cv::split(source, channels);  // B, G, R, A

        cv::Mat bgr;
        cv::merge(std::vector<cv::Mat>{channels[0], channels[1], channels[2]}, bgr);

        cv::Mat mask;
        cv::threshold(channels[3], mask, 32, 255, cv::THRESH_BINARY);

        const cv::Rect box = LargestContourBoundingBox(mask).Box;
        if (box.width <= 0 || box.height <= 0)
        {
            spdlog::error("{} has no opaque pixels - is it fully transparent?", path.string());
            continue;
        }

        Template newTemplate;
        newTemplate.PieceValue = pieceFile.PieceValue;
        cv::resize(bgr(box), newTemplate.Image, canonicalSize);
        cv::resize(mask(box), newTemplate.Mask, canonicalSize, 0, 0, cv::INTER_NEAREST);

        m_Templates.push_back(std::move(newTemplate));
    }

    return IsBootstrapped();
}

bool PieceTemplateLibrary::IsBootstrapped() const
{
    return !m_Templates.empty();
}

cv::Mat PieceTemplateLibrary::BuildForegroundMask(const cv::Mat& cell) const
{
    const cv::Vec3b reference = CellBackground::EstimateFromWholeCell(cell);

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

    // Fill the largest contour solid. Chess piece art always has a continuous dark outline
    // stroke, which is high-contrast against any background color - but a light piece's
    // interior fill can be nearly as light as a light square behind it, so the plain
    // diff-threshold above only reliably captures the outline and under-segments the
    // interior (confirmed against real captures: every white piece on a light square had
    // its mean color pulled dark enough by this to be mistaken for the black variant of the
    // same shape). Filling the outline's enclosed region recovers that interior regardless
    // of its own contrast, since anything inside the outline must belong to the piece.
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    if (!contours.empty())
    {
        size_t largestIndex = 0;
        double largestArea = 0.0;
        for (size_t i = 0; i < contours.size(); ++i)
        {
            const double area = cv::contourArea(contours[i]);
            if (area > largestArea)
            {
                largestArea = area;
                largestIndex = i;
            }
        }

        cv::drawContours(mask, contours, static_cast<int>(largestIndex), cv::Scalar(255), cv::FILLED);
    }

    return mask;
}

std::optional<Piece> PieceTemplateLibrary::Classify(const cv::Mat& cell, const cv::Vec3b& expectedBackground) const
{
    if (cell.empty() || m_Templates.empty())
        return std::nullopt;

    const cv::Mat mask = BuildForegroundMask(cell);

    const double foregroundRatio = static_cast<double>(cv::countNonZero(mask)) / static_cast<double>(mask.total());
    if (foregroundRatio < kMinForegroundRatio)
        return std::nullopt;  // empty square

    const ContourBox contour = LargestContourBoundingBox(mask);
    const cv::Rect box = contour.Box;
    if (box.width <= 0 || box.height <= 0)
        return std::nullopt;

    // A real piece silhouette is a solid, compact blob that mostly fills its own tight
    // bounding box. Sparse noise (e.g. a handful of scattered off-by-a-few-pixels artifacts
    // from real screen-capture/compression, spread out enough that a couple of them sit near
    // opposite corners) can still pass the foregroundRatio gate above and produce a huge
    // bounding box despite having almost no actual filled area - candidate content that's
    // mostly background color, upscaled to canonical size, has no business being compared
    // against dense piece templates at all. Require the contour to actually fill a
    // reasonable fraction of its own bounding box before trusting it as a piece.
    constexpr double kMinBoxFillRatio = 0.2;
    const double boxFillRatio = contour.Area / (static_cast<double>(box.width) * static_cast<double>(box.height));
    if (boxFillRatio < kMinBoxFillRatio)
        return std::nullopt;

    // Chess sites also render small non-piece indicators on some squares (e.g. a "legal
    // move" hint dot) - visually compact and solid enough to pass the fill-ratio check
    // above, but far smaller than any real piece silhouette, which is always drawn to
    // occupy the bulk of its square's height regardless of piece type (a real captured pawn
    // - the visually smallest/narrowest piece - measured at ~73% of cell height here, vs.
    // ~26% for a hint dot). Reject anything too small to plausibly be a piece.
    constexpr double kMinBoxHeightRatio = 0.35;
    if (static_cast<double>(box.height) / cell.rows < kMinBoxHeightRatio)
        return std::nullopt;

    // Undo any translucent highlight tint before comparing colors against templates: a
    // uniform tint shifts BOTH the background and any piece art on top of it by roughly the
    // same amount, so subtracting (this cell's own measured background - the board-wide
    // clean reference for its square color) from every pixel cancels that shift back out,
    // without needing to know the tint's color or opacity. The occupancy mask above is
    // unaffected by this - it already handles tinting via its own per-cell background - this
    // step only corrects the pixel colors that get compared against the (untinted) templates.
    const cv::Vec3b cellBackground = CellBackground::EstimateFromWholeCell(cell);
    const cv::Scalar tintOffset(cellBackground[0] - static_cast<double>(expectedBackground[0]), cellBackground[1] - static_cast<double>(expectedBackground[1]), cellBackground[2] - static_cast<double>(expectedBackground[2]));

    cv::Mat correctedCell;
    cv::subtract(cell, tintOffset, correctedCell);


    // Resized once here rather than once per template comparison below - see kCanonicalSize.
    const cv::Size canonicalSize(kCanonicalSize, kCanonicalSize);

    cv::Mat candidate;
    cv::Mat candidateMask;
    cv::resize(correctedCell(box), candidate, canonicalSize);
    cv::resize(mask(box), candidateMask, canonicalSize, 0, 0, cv::INTER_NEAREST);

    const std::vector<cv::Point> candidateContour = LargestContour(candidateMask);

    // Mean color within the candidate's own silhouette, not just the region it happens to
    // overlap with a given template - a white piece on a light square has low contrast
    // against its own background, so the diff-threshold mask under-segments the low-contrast
    // interior fill and mostly captures the (piece-color-independent) dark outline stroke;
    // comparing to each template's own full mean color is a coarser but far more robust
    // "is this piece light or dark overall" signal than a pixel-aligned comparison restricted
    // to whatever sliver happens to overlap both masks.
    const cv::Scalar candidateMeanColor = cv::mean(candidate, candidateMask);

    double bestDiff = std::numeric_limits<double>::max();
    std::optional<Piece> bestPiece;

    for (const Template& candidateTemplate : m_Templates)
    {
        cv::Mat combinedMask;
        cv::bitwise_and(candidateMask, candidateTemplate.Mask, combinedMask);

        if (cv::countNonZero(combinedMask) < 4)
            continue;  // not enough overlap to produce a meaningful score

        const cv::Scalar templateMeanColor = cv::mean(candidateTemplate.Image, candidateTemplate.Mask);
        double sumSquaredColorDiff = 0.0;
        for (int channel = 0; channel < 3; ++channel)
        {
            const double channelDiff = candidateMeanColor[channel] - templateMeanColor[channel];
            sumSquaredColorDiff += channelDiff * channelDiff;
        }
        const double colorDiff = sumSquaredColorDiff / (255.0 * 255.0 * 3.0);

        // Plain color-MSE alone systematically favors "boring" low-detail templates: a
        // black pawn's real art is nearly solid black with no internal shading, so it reads
        // as "close" to almost any dark candidate almost by default, while a black knight's
        // white eye/mane highlights only pay off if they land in exactly the right pixels -
        // confirmed against real captures, where the pawn template beat the CORRECT template
        // for knight, bishop, and rook candidates alike (even beating a white bishop's own
        // white-colored template in one case). Silhouette shape is what color-MSE can't see:
        // a pawn-shaped outline and a knight-shaped outline differ regardless of how flat
        // either one's internal coloring is. Hu-moment shape matching (not simple mask IoU)
        // because live-captured crops and reference-sprite crops aren't equally tight - a
        // real capture's box came from a diff threshold, a sprite's from its alpha channel -
        // so the same piece ends up a different apparent size in the two 48x48 canonical
        // images; IoU conflates that harmless scale mismatch with an actual shape mismatch,
        // while Hu moments are scale-invariant by construction.
        const std::vector<cv::Point> templateContour = LargestContour(candidateTemplate.Mask);
        const double shapeDiff = (!candidateContour.empty() && !templateContour.empty()) ? cv::matchShapes(candidateContour, templateContour, cv::CONTOURS_MATCH_I1, 0) : 1.0;

        const double diff = 0.5 * colorDiff + 0.5 * std::min(shapeDiff, 1.0);
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
