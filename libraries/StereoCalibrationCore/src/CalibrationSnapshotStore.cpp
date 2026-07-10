#include <VdcaStereoCalibration/CalibrationSnapshotStore.hpp>

#include <stdexcept>
#include <utility>

namespace Vdca::StereoCalibration {

std::shared_ptr<const CalibrationSnapshot> CalibrationSnapshotStore::publish(
    CalibrationDocument document,
    std::string activeProfile,
    bool estimatedFromLiveFrames) {
    ValidateCalibrationDocument(document);
    if (!document.hasProfile(activeProfile)) {
        throw std::invalid_argument("CalibrationSnapshotStore::publish: active profile does not exist");
    }

    auto immutableDocument = std::make_shared<const CalibrationDocument>(std::move(document));
    auto snapshot = std::make_shared<CalibrationSnapshot>();
    snapshot->document = std::move(immutableDocument);
    snapshot->activeProfile = std::move(activeProfile);
    snapshot->estimatedFromLiveFrames = estimatedFromLiveFrames;
    snapshot->publishedAt = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(mutex_);
    snapshot->revision = nextRevision_++;
    latest_ = snapshot;
    return latest_;
}

std::shared_ptr<const CalibrationSnapshot> CalibrationSnapshotStore::latest() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_;
}

} // namespace Vdca::StereoCalibration
