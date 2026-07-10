#include "StereoPlaneSurface.hpp"

#include <D3D12Helper/D3D12Gpu/D3D12Copy.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Helpers.hpp>

#include <stdexcept>
#include <string>

namespace Vdca {
namespace {

bool IsSupportedDisplayFormat(DXGI_FORMAT format) noexcept {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return true;
    default:
        return false;
    }
}

bool SameCopyShape(const D3D12_RESOURCE_DESC& a, const D3D12_RESOURCE_DESC& b) noexcept {
    return a.Dimension == b.Dimension &&
           a.Width == b.Width &&
           a.Height == b.Height &&
           a.DepthOrArraySize == b.DepthOrArraySize &&
           a.MipLevels == b.MipLevels &&
           a.Format == b.Format &&
           a.SampleDesc.Count == b.SampleDesc.Count &&
           a.SampleDesc.Quality == b.SampleDesc.Quality;
}

} // namespace

StereoPlaneSurface::StereoPlaneSurface(
    std::shared_ptr<D3D12CoreLib::D3D12Core> core,
    VarjoXR::Backends::D3D12::D3D12Backend& backend,
    VarjoXR::XRSpace& space,
    const StereoPlaneSurfaceDesc& desc)
    : core_(std::move(core)),
      width_(desc.width),
      height_(desc.height),
      format_(desc.format) {
    if (!core_) {
        throw std::invalid_argument("StereoPlaneSurface: core is null");
    }
    if (width_ == 0 || height_ == 0) {
        throw std::invalid_argument("StereoPlaneSurface: texture size must be non-zero");
    }
    if (!IsSupportedDisplayFormat(format_)) {
        throw std::invalid_argument(
            "StereoPlaneSurface: only R8G8B8A8_UNORM and B8G8R8A8_UNORM are supported");
    }
    if (!(desc.planeWidthMeters > 0.0f) || !(desc.planeDistanceMeters > 0.0f)) {
        throw std::invalid_argument("StereoPlaneSurface: plane width and distance must be positive");
    }

    copyContext_ = core_->CreateDirectContext();
    leftDisplayTexture_ = D3D12CoreLib::CreateTexture2D(
        *core_, width_, height_, format_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    rightDisplayTexture_ = D3D12CoreLib::CreateTexture2D(
        *core_, width_, height_, format_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    leftXrTexture_ = backend.wrapResource(leftDisplayTexture_.Get(), format_);
    rightXrTexture_ = backend.wrapResource(rightDisplayTexture_.Get(), format_);

    const float aspect = static_cast<float>(height_) / static_cast<float>(width_);
    plane_ = &space.createPlane({desc.planeWidthMeters, desc.planeWidthMeters * aspect});
    plane_->setPlacementMode(desc.placementMode);
    plane_->transform().position = {
        0.0f,
        desc.planeVerticalOffsetMeters,
        -desc.planeDistanceMeters,
    };
    plane_->setTexture(VarjoXR::Eye::Left, leftXrTexture_);
    plane_->setTexture(VarjoXR::Eye::Right, rightXrTexture_);
}

StereoPlaneSurface::~StereoPlaneSurface() {
    try {
        waitForPreviousCopy();
    } catch (...) {
        // Destructors must not throw. The D3D12 debug layer/DRED remains the
        // authoritative source if device shutdown also reports an error.
    }
}

void StereoPlaneSurface::validateFrame(
    const MFFrameSource::MFD3D12CameraFrame& frame,
    const char* eyeName) const {
    if (!frame) {
        throw std::runtime_error(std::string("StereoPlaneSurface: empty ") + eyeName + " frame");
    }
    if (frame.width() != width_ || frame.height() != height_ || frame.format() != format_) {
        throw std::runtime_error(std::string("StereoPlaneSurface: ") + eyeName +
                                 " frame size/format changed");
    }

    const auto& source = frame.resource();
    const auto& destination = (eyeName[0] == 'l') ? leftDisplayTexture_ : rightDisplayTexture_;
    if (!SameCopyShape(source.GetDesc(), destination.GetDesc())) {
        throw std::runtime_error(std::string("StereoPlaneSurface: ") + eyeName +
                                 " resource is not copy-compatible with the display texture");
    }
}

void StereoPlaneSurface::waitForPreviousCopy() {
    if (previousCopyFenceValue_ != 0 && core_) {
        core_->DirectQueue().WaitForFenceValue(previousCopyFenceValue_);
        previousCopyFenceValue_ = 0;
    }
}

void StereoPlaneSurface::updateFromSynchronizedFrame(
    MFFrameSource::MFD3D12StereoFrame& frame) {
    if (!frame) {
        throw std::invalid_argument("StereoPlaneSurface: synchronized frame is empty");
    }

    validateFrame(frame.left, "left");
    validateFrame(frame.right, "right");

    // The MFFrameSource producer, this copy, and VarjoXR rendering all use the
    // same D3D12Core direct queue. Queue submission order therefore provides
    // GPU-to-GPU synchronization without a per-frame CPU waitReady().
    waitForPreviousCopy();
    copyContext_.Reset();

    auto& leftSource = frame.left.resource();
    auto& rightSource = frame.right.resource();
    const auto leftSourceState = leftSource.GetState();
    const auto rightSourceState = rightSource.GetState();

    D3D12CoreLib::RecordCopyResource(
        copyContext_,
        leftDisplayTexture_,
        leftSource,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        leftSourceState);
    D3D12CoreLib::RecordCopyResource(
        copyContext_,
        rightDisplayTexture_,
        rightSource,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        rightSourceState);

    copyContext_.Close();
    ID3D12CommandList* commandLists[] = {copyContext_.GetCommandList()};
    core_->DirectQueue().ExecuteCommandLists(1, commandLists);
    previousCopyFenceValue_ = core_->DirectQueue().Signal();

    lastPairNumber_ = frame.pairNumber;
    lastAdjustedDiff100ns_ = frame.adjustedDiff100ns;
}

void StereoPlaneSurface::setProcessing(
    VarjoXR::Eye eye,
    const VarjoXR::TextureProcessingDesc& processing) {
    if (!plane_) {
        throw std::runtime_error("StereoPlaneSurface: plane is not initialized");
    }
    plane_->setProcessing(eye, processing);
}

} // namespace Vdca
