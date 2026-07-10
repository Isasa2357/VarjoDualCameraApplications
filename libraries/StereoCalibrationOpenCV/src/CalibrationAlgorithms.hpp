#pragma once

#include <VdcaStereoCalibration/CalibrationDocument.hpp>

#include <opencv2/core.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace Vdca::StereoCalibration::internal {

struct Observation {
    std::vector<cv::Point2f> left;
    std::vector<cv::Point2f> right;
};

bool DetectObservation(
    const cv::Mat& leftGray,
    const cv::Mat& rightGray,
    std::uint32_t boardColumns,
    std::uint32_t boardRows,
    const std::string& rightOrder,
    bool useFindChessboardCornersSB,
    std::uint32_t detectionMaxDimension,
    Observation& output);

double MeanCornerMotion(const Observation& a, const Observation& b);

std::size_t UpdateEstimatedProfiles(
    CalibrationDocument& document,
    const std::deque<Observation>& observations,
    ImageSize processingInputSize,
    ImageSize rectifiedOutputSize,
    double fundamentalRansacThresholdPx,
    bool fitUncalibratedResultToCanvas);

} // namespace Vdca::StereoCalibration::internal
