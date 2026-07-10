#pragma once

#include <VdcaStereoCalibration/CalibrationDocument.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace Vdca::StereoCalibration {

struct CalibrationSnapshot {
    std::uint64_t revision = 0;
    std::shared_ptr<const CalibrationDocument> document;
    std::string activeProfile;
    bool estimatedFromLiveFrames = false;
    std::chrono::steady_clock::time_point publishedAt{};

    explicit operator bool() const noexcept {
        return revision != 0 && static_cast<bool>(document);
    }

    nlohmann::json toJson() const {
        return document ? document->toJson() : nlohmann::json{};
    }
};

class CalibrationSnapshotStore {
public:
    std::shared_ptr<const CalibrationSnapshot> publish(
        CalibrationDocument document,
        std::string activeProfile,
        bool estimatedFromLiveFrames);

    std::shared_ptr<const CalibrationSnapshot> latest() const;

private:
    mutable std::mutex mutex_;
    std::shared_ptr<const CalibrationSnapshot> latest_;
    std::uint64_t nextRevision_ = 1;
};

} // namespace Vdca::StereoCalibration
