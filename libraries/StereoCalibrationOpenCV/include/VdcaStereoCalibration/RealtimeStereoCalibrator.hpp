#pragma once

#include <VdcaStereoCalibration/CalibrationSnapshotStore.hpp>
#include <VdcaStereoCalibration/D3D12StereoFrame.hpp>

#include <D3D12Helper/D3D12Core/D3D12Core.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>

namespace Vdca::StereoCalibration {

struct RealtimeStereoCalibratorConfig {
    std::shared_ptr<D3D12CoreLib::D3D12Core> d3d12;

    ImageSize sourceSize;
    ImageSize processingInputSize;
    ImageSize rectifiedOutputSize;

    std::uint32_t boardColumns = 12;
    std::uint32_t boardRows = 9;
    std::string rightOrder = "same";
    std::string activeProfile = "uncalibrated";

    std::size_t maxObservationCount = 30;
    std::size_t minObservationCountForUpdate = 1;
    double minMeanCornerMotionPx = 2.0;
    double fundamentalRansacThresholdPx = 1.5;
    bool fitUncalibratedResultToCanvas = true;
    bool useFindChessboardCornersSB = true;

    std::array<float, 4> borderRgba{0.0f, 0.0f, 0.0f, 1.0f};
    std::optional<CalibrationDocument> initialCalibration;
};

struct RealtimeStereoCalibratorStats {
    std::uint64_t submittedFrames = 0;
    std::uint64_t replacedPendingFrames = 0;
    std::uint64_t analyzedFrames = 0;
    std::uint64_t checkerboardMisses = 0;
    std::uint64_t duplicateObservations = 0;
    std::uint64_t acceptedObservations = 0;
    std::uint64_t publishedRevisions = 0;
    std::uint64_t estimationFailures = 0;
};

class RealtimeStereoCalibrator {
public:
    explicit RealtimeStereoCalibrator(RealtimeStereoCalibratorConfig config);
    ~RealtimeStereoCalibrator();

    RealtimeStereoCalibrator(const RealtimeStereoCalibrator&) = delete;
    RealtimeStereoCalibrator& operator=(const RealtimeStereoCalibrator&) = delete;

    void start();
    void requestStop() noexcept;
    void stop();

    // Replaces an older unprocessed submission. The worker always analyzes the
    // newest frame available after its current calculation finishes.
    void submitLatestFrame(StereoD3D12Frame frame);

    std::shared_ptr<const CalibrationSnapshot> latestSnapshot() const;
    nlohmann::json latestJson() const;
    RealtimeStereoCalibratorStats stats() const noexcept;
    std::exception_ptr workerException() const;
    void rethrowWorkerExceptionIfAny() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Vdca::StereoCalibration
