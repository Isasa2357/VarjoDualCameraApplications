#include <VdcaStereoCalibration/RealtimeStereoCalibrator.hpp>

#include "CalibrationAlgorithms.hpp"
#include "D3D12StereoReadback.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace Vdca::StereoCalibration {
namespace {

void ValidateConfig(const RealtimeStereoCalibratorConfig& config) {
    if (!config.d3d12) throw std::invalid_argument("RealtimeStereoCalibrator: D3D12Core is null");
    if (!config.sourceSize.valid() || !config.processingInputSize.valid() || !config.rectifiedOutputSize.valid()) {
        throw std::invalid_argument("RealtimeStereoCalibrator: image sizes must be non-zero");
    }
    if (config.boardColumns < 2 || config.boardRows < 2) {
        throw std::invalid_argument("RealtimeStereoCalibrator: checkerboard dimensions must be at least 2x2");
    }
    if (config.maxObservationCount == 0 || config.minObservationCountForUpdate == 0 ||
        config.minObservationCountForUpdate > config.maxObservationCount) {
        throw std::invalid_argument("RealtimeStereoCalibrator: invalid observation counts");
    }
    if (config.activeProfile != "uncalibrated" && config.activeProfile != "affine_vertical" &&
        config.activeProfile != "affine_full") {
        throw std::invalid_argument("RealtimeStereoCalibrator: unsupported active profile");
    }
    if (config.rightOrder != "same" && config.rightOrder != "flip_x" &&
        config.rightOrder != "flip_y" && config.rightOrder != "rot180") {
        throw std::invalid_argument("RealtimeStereoCalibrator: unsupported right order");
    }
}

} // namespace

struct RealtimeStereoCalibrator::Impl {
    explicit Impl(RealtimeStereoCalibratorConfig value)
        : config(std::move(value)), readback(config.d3d12) {
        ValidateConfig(config);
        CalibrationDocument initial;
        if (config.initialCalibration) {
            initial = *config.initialCalibration;
            ValidateCalibrationDocument(initial);
            if (initial.sourceSize.width != config.sourceSize.width ||
                initial.sourceSize.height != config.sourceSize.height ||
                initial.calibrationInputSize.width != config.processingInputSize.width ||
                initial.calibrationInputSize.height != config.processingInputSize.height ||
                initial.rectifiedOutputSize.width != config.rectifiedOutputSize.width ||
                initial.rectifiedOutputSize.height != config.rectifiedOutputSize.height) {
                throw std::invalid_argument("RealtimeStereoCalibrator: initial JSON geometry mismatch");
            }
            if (!initial.hasProfile(config.activeProfile)) {
                throw std::invalid_argument("RealtimeStereoCalibrator: initial JSON lacks active profile");
            }
        } else {
            initial = MakeIdentityCalibrationDocument(
                config.sourceSize,
                config.processingInputSize,
                config.rectifiedOutputSize,
                config.boardColumns,
                config.boardRows,
                config.rightOrder,
                config.activeProfile);

            const double sx = static_cast<double>(config.rectifiedOutputSize.width) /
                              static_cast<double>(config.processingInputSize.width);
            const double sy = static_cast<double>(config.rectifiedOutputSize.height) /
                              static_cast<double>(config.processingInputSize.height);
            Homography3x3 forward;
            forward.rows = {
                sx, 0.0, 0.0,
                0.0, sy, 0.0,
                0.0, 0.0, 1.0,
            };
            const Homography3x3 inverse = InvertHomography(forward);
            for (auto& [name, profile] : initial.profiles) {
                (void)name;
                profile.leftForward = forward;
                profile.leftInverse = inverse;
                profile.rightForward = forward;
                profile.rightInverse = inverse;
            }
        }
        store.publish(std::move(initial), config.activeProfile, false);
    }

    void start() {
        bool expected = false;
        if (!running.compare_exchange_strong(expected, true)) return;
        stopRequested.store(false);
        worker = std::thread([this] { run(); });
    }

    void requestStop() noexcept {
        stopRequested.store(true);
        pendingCv.notify_all();
    }

    void stop() {
        requestStop();
        if (worker.joinable()) worker.join();
        running.store(false);
    }

    void submit(StereoD3D12Frame frame) {
        if (!frame) throw std::invalid_argument("RealtimeStereoCalibrator::submitLatestFrame: empty frame");
        if (frame.left.width != config.processingInputSize.width ||
            frame.left.height != config.processingInputSize.height ||
            frame.right.width != config.processingInputSize.width ||
            frame.right.height != config.processingInputSize.height) {
            throw std::invalid_argument("RealtimeStereoCalibrator::submitLatestFrame: frame-size mismatch");
        }
        submittedFrames.fetch_add(1);
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            if (pendingFrame) replacedPendingFrames.fetch_add(1);
            pendingFrame = std::move(frame);
        }
        pendingCv.notify_one();
    }

    void run() noexcept {
        try {
            while (!stopRequested.load()) {
                std::optional<StereoD3D12Frame> frame;
                {
                    std::unique_lock<std::mutex> lock(pendingMutex);
                    pendingCv.wait(lock, [this] { return stopRequested.load() || pendingFrame.has_value(); });
                    if (stopRequested.load()) break;
                    frame = std::move(pendingFrame);
                    pendingFrame.reset();
                }
                if (!frame) continue;
                try {
                    analyze(*frame);
                } catch (...) {
                    estimationFailures.fetch_add(1);
                }
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(exceptionMutex);
            exception = std::current_exception();
        }
    }

    void analyze(const StereoD3D12Frame& frame) {
        auto [leftGray, rightGray] = readback.readGray(frame);
        analyzedFrames.fetch_add(1);

        internal::Observation observation;
        if (!internal::DetectObservation(
                leftGray,
                rightGray,
                config.boardColumns,
                config.boardRows,
                config.rightOrder,
                config.useFindChessboardCornersSB,
                config.checkerboardDetectionMaxDimension,
                observation)) {
            checkerboardMisses.fetch_add(1);
            return;
        }
        if (!observations.empty() &&
            internal::MeanCornerMotion(observations.back(), observation) < config.minMeanCornerMotionPx) {
            duplicateObservations.fetch_add(1);
            return;
        }

        observations.push_back(std::move(observation));
        while (observations.size() > config.maxObservationCount) observations.pop_front();
        acceptedObservations.fetch_add(1);
        if (observations.size() < config.minObservationCountForUpdate) return;

        const auto latest = store.latest();
        CalibrationDocument document = latest && latest->document
            ? *latest->document
            : MakeIdentityCalibrationDocument(
                  config.sourceSize,
                  config.processingInputSize,
                  config.rectifiedOutputSize,
                  config.boardColumns,
                  config.boardRows,
                  config.rightOrder,
                  config.activeProfile);
        document.generatorName = "RealtimeStereoCalibration";
        document.generatorVersion = 1;
        document.defaultProfile = config.activeProfile;
        document.sourceSize = config.sourceSize;
        document.calibrationInputSize = config.processingInputSize;
        document.rectifiedOutputSize = config.rectifiedOutputSize;
        document.boardColumns = config.boardColumns;
        document.boardRows = config.boardRows;
        document.rightOrder = config.rightOrder;
        document.borderRgba = config.borderRgba;

        const std::size_t successCount = internal::UpdateEstimatedProfiles(
            document,
            observations,
            config.processingInputSize,
            config.rectifiedOutputSize,
            config.fundamentalRansacThresholdPx,
            config.fitUncalibratedResultToCanvas);
        if (successCount == 0) throw std::runtime_error("all calibration estimators failed");

        store.publish(std::move(document), config.activeProfile, true);
        publishedRevisions.fetch_add(1);
    }

    RealtimeStereoCalibratorStats getStats() const noexcept {
        return {
            submittedFrames.load(),
            replacedPendingFrames.load(),
            analyzedFrames.load(),
            checkerboardMisses.load(),
            duplicateObservations.load(),
            acceptedObservations.load(),
            publishedRevisions.load(),
            estimationFailures.load(),
        };
    }

    RealtimeStereoCalibratorConfig config;
    internal::StereoGpuReadback readback;
    CalibrationSnapshotStore store;
    std::deque<internal::Observation> observations;

    std::atomic_bool running{false};
    std::atomic_bool stopRequested{false};
    std::thread worker;
    std::mutex pendingMutex;
    std::condition_variable pendingCv;
    std::optional<StereoD3D12Frame> pendingFrame;

    mutable std::mutex exceptionMutex;
    std::exception_ptr exception;

    std::atomic<std::uint64_t> submittedFrames{0};
    std::atomic<std::uint64_t> replacedPendingFrames{0};
    std::atomic<std::uint64_t> analyzedFrames{0};
    std::atomic<std::uint64_t> checkerboardMisses{0};
    std::atomic<std::uint64_t> duplicateObservations{0};
    std::atomic<std::uint64_t> acceptedObservations{0};
    std::atomic<std::uint64_t> publishedRevisions{0};
    std::atomic<std::uint64_t> estimationFailures{0};
};

RealtimeStereoCalibrator::RealtimeStereoCalibrator(RealtimeStereoCalibratorConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

RealtimeStereoCalibrator::~RealtimeStereoCalibrator() {
    if (impl_) impl_->stop();
}

void RealtimeStereoCalibrator::start() { impl_->start(); }
void RealtimeStereoCalibrator::requestStop() noexcept { impl_->requestStop(); }
void RealtimeStereoCalibrator::stop() { impl_->stop(); }
void RealtimeStereoCalibrator::submitLatestFrame(StereoD3D12Frame frame) { impl_->submit(std::move(frame)); }
std::shared_ptr<const CalibrationSnapshot> RealtimeStereoCalibrator::latestSnapshot() const { return impl_->store.latest(); }

nlohmann::json RealtimeStereoCalibrator::latestJson() const {
    const auto snapshot = latestSnapshot();
    return snapshot ? snapshot->toJson() : nlohmann::json{};
}

RealtimeStereoCalibratorStats RealtimeStereoCalibrator::stats() const noexcept { return impl_->getStats(); }

std::exception_ptr RealtimeStereoCalibrator::workerException() const {
    std::lock_guard<std::mutex> lock(impl_->exceptionMutex);
    return impl_->exception;
}

void RealtimeStereoCalibrator::rethrowWorkerExceptionIfAny() const {
    if (auto value = workerException()) std::rethrow_exception(value);
}

} // namespace Vdca::StereoCalibration
