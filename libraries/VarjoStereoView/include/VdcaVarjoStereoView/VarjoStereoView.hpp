#pragma once

#include <VdcaStereoCalibration/CalibrationSnapshotStore.hpp>
#include <VdcaStereoCalibration/D3D12StereoFrame.hpp>

#include <D3D12Helper/D3D12Core/D3D12CommandContext.hpp>
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Core/D3D12Fence.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Resource.hpp>

#include <VarjoXR/VarjoXR.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Vdca::VarjoStereoView {

struct ViewConfig {
    StereoCalibration::ImageSize inputSize;
    StereoCalibration::ImageSize rectifiedOutputSize;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;

    float planeWidthMeters = 1.0f;
    float planeDistanceMeters = 1.0f;
    float planeVerticalOffsetMeters = 0.0f;
    VarjoXR::PlacementMode placementMode = VarjoXR::PlacementMode::HeadRelative;
};

class VarjoStereoView {
public:
    VarjoStereoView(
        std::shared_ptr<D3D12CoreLib::D3D12Core> core,
        VarjoXR::Backends::D3D12::D3D12Backend& backend,
        VarjoXR::XRSpace& space,
        ViewConfig config);
    ~VarjoStereoView();

    VarjoStereoView(const VarjoStereoView&) = delete;
    VarjoStereoView& operator=(const VarjoStereoView&) = delete;

    // Copies the supplied resources into view-owned stable textures. The
    // optional lifetime token is held until the GPU copy completes.
    void submitFrame(StereoCalibration::StereoD3D12Frame frame);

    // Updates both eye transforms as one revision. Subsequent submitted frames
    // are rendered with this calibration until a newer snapshot is applied.
    void applyCalibration(const StereoCalibration::CalibrationSnapshot& snapshot);
    void clearCalibration();

    std::uint64_t appliedCalibrationRevision() const noexcept { return appliedRevision_; }
    std::uint64_t lastSubmittedFrameNumber() const noexcept { return lastFrameNumber_; }
    VarjoXR::XRPlane& plane() noexcept { return *plane_; }

private:
    void waitForPreviousCopy();
    void validateFrame(const StereoCalibration::StereoD3D12Frame& frame) const;

    std::shared_ptr<D3D12CoreLib::D3D12Core> core_;
    D3D12CoreLib::D3D12CommandContext copyContext_;
    D3D12CoreLib::D3D12Fence copyFence_;
    UINT64 copyFenceValue_ = 0;

    D3D12CoreLib::D3D12Resource leftTexture_;
    D3D12CoreLib::D3D12Resource rightTexture_;
    std::array<std::shared_ptr<VarjoXR::Backends::D3D12::D3D12Texture>, 2> leftAliases_;
    std::array<std::shared_ptr<VarjoXR::Backends::D3D12::D3D12Texture>, 2> rightAliases_;
    std::size_t aliasIndex_ = 0;
    std::shared_ptr<void> inFlightLifetimeToken_;

    ViewConfig config_;
    VarjoXR::XRPlane* plane_ = nullptr;
    std::uint64_t appliedRevision_ = 0;
    std::uint64_t lastFrameNumber_ = 0;
};

} // namespace Vdca::VarjoStereoView
