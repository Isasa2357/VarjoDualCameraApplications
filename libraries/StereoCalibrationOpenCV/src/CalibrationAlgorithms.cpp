#include "CalibrationAlgorithms.hpp"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace Vdca::StereoCalibration::internal {
namespace {

bool DetectCorners(const cv::Mat& gray, cv::Size board, bool useSb, std::vector<cv::Point2f>& corners) {
    if (useSb) {
        const int flags = cv::CALIB_CB_EXHAUSTIVE | cv::CALIB_CB_ACCURACY;
        if (cv::findChessboardCornersSB(gray, board, corners, flags)) return true;
        corners.clear();
    }
    const int flags = cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE;
    if (!cv::findChessboardCorners(gray, board, corners, flags)) {
        corners.clear();
        return false;
    }
    cv::cornerSubPix(
        gray, corners, cv::Size(11, 11), cv::Size(-1, -1),
        cv::TermCriteria(cv::TermCriteria::EPS | cv::TermCriteria::COUNT, 40, 1.0e-4));
    return true;
}

std::vector<cv::Point2f> ApplyRightOrder(
    const std::vector<cv::Point2f>& corners,
    std::uint32_t columns,
    std::uint32_t rows,
    const std::string& order) {
    if (corners.size() != static_cast<std::size_t>(columns) * rows) {
        throw std::invalid_argument("ApplyRightOrder: unexpected corner count");
    }
    std::vector<cv::Point2f> result(corners.size());
    for (std::uint32_t r = 0; r < rows; ++r) {
        for (std::uint32_t c = 0; c < columns; ++c) {
            std::uint32_t sourceR = r;
            std::uint32_t sourceC = c;
            if (order == "flip_x" || order == "rot180") sourceC = columns - 1u - c;
            if (order == "flip_y" || order == "rot180") sourceR = rows - 1u - r;
            result[static_cast<std::size_t>(r) * columns + c] =
                corners[static_cast<std::size_t>(sourceR) * columns + sourceC];
        }
    }
    return result;
}

void Concatenate(
    const std::deque<Observation>& observations,
    std::vector<cv::Point2f>& left,
    std::vector<cv::Point2f>& right) {
    std::size_t total = 0;
    for (const auto& value : observations) total += value.left.size();
    left.clear();
    right.clear();
    left.reserve(total);
    right.reserve(total);
    for (const auto& value : observations) {
        left.insert(left.end(), value.left.begin(), value.left.end());
        right.insert(right.end(), value.right.begin(), value.right.end());
    }
}

Homography3x3 ToHomography(const cv::Mat& matrix) {
    if (matrix.rows != 3 || matrix.cols != 3) throw std::invalid_argument("expected 3x3 matrix");
    cv::Mat converted;
    matrix.convertTo(converted, CV_64F);
    Homography3x3 result;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            result.rows[static_cast<std::size_t>(r) * 3u + c] = converted.at<double>(r, c);
        }
    }
    return result;
}

std::pair<double, double> VerticalError(
    const cv::Mat& leftH,
    const cv::Mat& rightH,
    const std::vector<cv::Point2f>& left,
    const std::vector<cv::Point2f>& right) {
    std::vector<cv::Point2f> transformedLeft;
    std::vector<cv::Point2f> transformedRight;
    cv::perspectiveTransform(left, transformedLeft, leftH);
    cv::perspectiveTransform(right, transformedRight, rightH);
    std::vector<double> errors;
    errors.reserve(transformedLeft.size());
    double sum = 0.0;
    for (std::size_t i = 0; i < transformedLeft.size(); ++i) {
        const double error = std::abs(static_cast<double>(transformedLeft[i].y - transformedRight[i].y));
        errors.push_back(error);
        sum += error;
    }
    if (errors.empty()) throw std::runtime_error("no points for vertical error");
    const double mean = sum / static_cast<double>(errors.size());
    const auto middle = errors.begin() + static_cast<std::ptrdiff_t>(errors.size() / 2u);
    std::nth_element(errors.begin(), middle, errors.end());
    double median = *middle;
    if ((errors.size() % 2u) == 0u) {
        median = (*std::max_element(errors.begin(), middle) + *middle) * 0.5;
    }
    return {mean, median};
}

cv::Mat ScaleToOutput(ImageSize input, ImageSize output) {
    const double sx = static_cast<double>(output.width) / static_cast<double>(input.width);
    const double sy = static_cast<double>(output.height) / static_cast<double>(input.height);
    return (cv::Mat_<double>(3, 3) <<
        sx, 0.0, 0.0,
        0.0, sy, 0.0,
        0.0, 0.0, 1.0);
}

cv::Mat FitCommonCanvas(
    const cv::Mat& leftH,
    const cv::Mat& rightH,
    ImageSize input,
    ImageSize output) {
    std::vector<cv::Point2f> corners{
        {0.0f, 0.0f},
        {static_cast<float>(input.width - 1u), 0.0f},
        {static_cast<float>(input.width - 1u), static_cast<float>(input.height - 1u)},
        {0.0f, static_cast<float>(input.height - 1u)},
    };
    std::vector<cv::Point2f> combined;
    std::vector<cv::Point2f> transformed;
    cv::perspectiveTransform(corners, combined, leftH);
    cv::perspectiveTransform(corners, transformed, rightH);
    combined.insert(combined.end(), transformed.begin(), transformed.end());

    double minX = std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
    for (const auto& point : combined) {
        minX = std::min(minX, static_cast<double>(point.x));
        minY = std::min(minY, static_cast<double>(point.y));
        maxX = std::max(maxX, static_cast<double>(point.x));
        maxY = std::max(maxY, static_cast<double>(point.y));
    }
    const double extentX = std::max(1.0, maxX - minX);
    const double extentY = std::max(1.0, maxY - minY);
    const double scale = std::min(
        static_cast<double>(output.width) / extentX,
        static_cast<double>(output.height) / extentY);
    return (cv::Mat_<double>(3, 3) <<
        scale, 0.0, -minX * scale,
        0.0, scale, -minY * scale,
        0.0, 0.0, 1.0);
}

RectificationProfile EstimateAffineVertical(
    const std::vector<cv::Point2f>& left,
    const std::vector<cv::Point2f>& right,
    std::size_t observationCount,
    ImageSize inputSize,
    ImageSize outputSize) {
    cv::Mat design(static_cast<int>(right.size()), 3, CV_64F);
    cv::Mat target(static_cast<int>(right.size()), 1, CV_64F);
    for (int i = 0; i < static_cast<int>(right.size()); ++i) {
        design.at<double>(i, 0) = right[static_cast<std::size_t>(i)].x;
        design.at<double>(i, 1) = right[static_cast<std::size_t>(i)].y;
        design.at<double>(i, 2) = 1.0;
        target.at<double>(i, 0) = left[static_cast<std::size_t>(i)].y;
    }
    cv::Mat coefficients;
    if (!cv::solve(design, target, coefficients, cv::DECOMP_SVD)) {
        throw std::runtime_error("affine_vertical solve failed");
    }
    cv::Mat leftH = cv::Mat::eye(3, 3, CV_64F);
    cv::Mat rightH = cv::Mat::eye(3, 3, CV_64F);
    rightH.at<double>(1, 0) = coefficients.at<double>(0, 0);
    rightH.at<double>(1, 1) = coefficients.at<double>(1, 0);
    rightH.at<double>(1, 2) = coefficients.at<double>(2, 0);
    const cv::Mat scale = ScaleToOutput(inputSize, outputSize);
    leftH = scale * leftH;
    rightH = scale * rightH;
    const auto [mean, median] = VerticalError(leftH, rightH, left, right);

    RectificationProfile profile;
    profile.method = "affine_vertical";
    profile.parameters = {{"estimator", "least_squares"}};
    profile.quality = {
        static_cast<std::uint32_t>(observationCount),
        static_cast<std::uint32_t>(left.size()),
        static_cast<std::uint32_t>(left.size()),
        mean, median};
    profile.leftForward = ToHomography(leftH);
    profile.leftInverse = InvertHomography(profile.leftForward);
    profile.rightForward = ToHomography(rightH);
    profile.rightInverse = InvertHomography(profile.rightForward);
    return profile;
}

RectificationProfile EstimateAffineFull(
    const std::vector<cv::Point2f>& left,
    const std::vector<cv::Point2f>& right,
    std::size_t observationCount,
    ImageSize inputSize,
    ImageSize outputSize) {
    cv::Mat inlierMask;
    cv::Mat affine = cv::estimateAffinePartial2D(
        right, left, inlierMask, cv::RANSAC, 2.5, 5000, 0.999, 20);
    if (affine.empty()) throw std::runtime_error("estimateAffinePartial2D failed");
    cv::Mat leftH = cv::Mat::eye(3, 3, CV_64F);
    cv::Mat rightH = cv::Mat::eye(3, 3, CV_64F);
    cv::Mat affine64;
    affine.convertTo(affine64, CV_64F);
    affine64.copyTo(rightH(cv::Rect(0, 0, 3, 2)));
    const cv::Mat scale = ScaleToOutput(inputSize, outputSize);
    leftH = scale * leftH;
    rightH = scale * rightH;
    const auto [mean, median] = VerticalError(leftH, rightH, left, right);

    RectificationProfile profile;
    profile.method = "affine_full";
    profile.parameters = {
        {"estimator", "estimateAffinePartial2D"},
        {"ransac_threshold_px", 2.5},
        {"confidence", 0.999}};
    profile.quality = {
        static_cast<std::uint32_t>(observationCount),
        static_cast<std::uint32_t>(left.size()),
        static_cast<std::uint32_t>(inlierMask.empty() ? left.size() : cv::countNonZero(inlierMask)),
        mean, median};
    profile.leftForward = ToHomography(leftH);
    profile.leftInverse = InvertHomography(profile.leftForward);
    profile.rightForward = ToHomography(rightH);
    profile.rightInverse = InvertHomography(profile.rightForward);
    return profile;
}

RectificationProfile EstimateUncalibrated(
    const std::vector<cv::Point2f>& left,
    const std::vector<cv::Point2f>& right,
    std::size_t observationCount,
    ImageSize inputSize,
    ImageSize outputSize,
    double threshold,
    bool fitCanvas) {
    cv::Mat mask;
    cv::Mat fundamental = cv::findFundamentalMat(
        left, right, cv::FM_RANSAC, threshold, 0.999, mask);
    if (fundamental.empty() || fundamental.rows != 3 || fundamental.cols != 3) {
        throw std::runtime_error("findFundamentalMat failed");
    }
    std::vector<cv::Point2f> inlierLeft;
    std::vector<cv::Point2f> inlierRight;
    for (int i = 0; i < static_cast<int>(left.size()); ++i) {
        if (mask.empty() || mask.at<std::uint8_t>(i) != 0) {
            inlierLeft.push_back(left[static_cast<std::size_t>(i)]);
            inlierRight.push_back(right[static_cast<std::size_t>(i)]);
        }
    }
    if (inlierLeft.size() < 8) throw std::runtime_error("too few fundamental-matrix inliers");

    cv::Mat leftH;
    cv::Mat rightH;
    if (!cv::stereoRectifyUncalibrated(
            inlierLeft, inlierRight, fundamental,
            cv::Size(static_cast<int>(inputSize.width), static_cast<int>(inputSize.height)),
            leftH, rightH)) {
        throw std::runtime_error("stereoRectifyUncalibrated failed");
    }
    leftH.convertTo(leftH, CV_64F);
    rightH.convertTo(rightH, CV_64F);
    const cv::Mat transform = fitCanvas
        ? FitCommonCanvas(leftH, rightH, inputSize, outputSize)
        : ScaleToOutput(inputSize, outputSize);
    leftH = transform * leftH;
    rightH = transform * rightH;
    const auto [mean, median] = VerticalError(leftH, rightH, left, right);

    RectificationProfile profile;
    profile.method = "uncalibrated";
    profile.parameters = {
        {"ransac_threshold_px", threshold},
        {"fit_canvas", fitCanvas}};
    profile.quality = {
        static_cast<std::uint32_t>(observationCount),
        static_cast<std::uint32_t>(left.size()),
        static_cast<std::uint32_t>(inlierLeft.size()),
        mean, median};
    profile.leftForward = ToHomography(leftH);
    profile.leftInverse = InvertHomography(profile.leftForward);
    profile.rightForward = ToHomography(rightH);
    profile.rightInverse = InvertHomography(profile.rightForward);
    return profile;
}

} // namespace

bool DetectObservation(
    const cv::Mat& leftGray,
    const cv::Mat& rightGray,
    std::uint32_t boardColumns,
    std::uint32_t boardRows,
    const std::string& rightOrder,
    bool useFindChessboardCornersSB,
    Observation& output) {
    const cv::Size board(static_cast<int>(boardColumns), static_cast<int>(boardRows));
    if (!DetectCorners(leftGray, board, useFindChessboardCornersSB, output.left) ||
        !DetectCorners(rightGray, board, useFindChessboardCornersSB, output.right)) {
        output = {};
        return false;
    }
    output.right = ApplyRightOrder(output.right, boardColumns, boardRows, rightOrder);
    return true;
}

double MeanCornerMotion(const Observation& a, const Observation& b) {
    if (a.left.size() != b.left.size() || a.right.size() != b.right.size() || a.left.empty()) {
        return std::numeric_limits<double>::infinity();
    }
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = 0; i < a.left.size(); ++i) {
        sum += cv::norm(a.left[i] - b.left[i]);
        sum += cv::norm(a.right[i] - b.right[i]);
        count += 2;
    }
    return sum / static_cast<double>(count);
}

std::size_t UpdateEstimatedProfiles(
    CalibrationDocument& document,
    const std::deque<Observation>& observations,
    ImageSize processingInputSize,
    ImageSize rectifiedOutputSize,
    double fundamentalRansacThresholdPx,
    bool fitUncalibratedResultToCanvas) {
    std::vector<cv::Point2f> left;
    std::vector<cv::Point2f> right;
    Concatenate(observations, left, right);
    std::size_t successCount = 0;
    try {
        document.profiles["affine_vertical"] = EstimateAffineVertical(
            left, right, observations.size(), processingInputSize, rectifiedOutputSize);
        ++successCount;
    } catch (...) {}
    try {
        document.profiles["affine_full"] = EstimateAffineFull(
            left, right, observations.size(), processingInputSize, rectifiedOutputSize);
        ++successCount;
    } catch (...) {}
    try {
        document.profiles["uncalibrated"] = EstimateUncalibrated(
            left, right, observations.size(), processingInputSize, rectifiedOutputSize,
            fundamentalRansacThresholdPx, fitUncalibratedResultToCanvas);
        ++successCount;
    } catch (...) {}
    return successCount;
}

} // namespace Vdca::StereoCalibration::internal
