#pragma once

#include <MFFrameSource/MFD3D12CameraSyncThread.hpp>

#include <D3D12Helper/D3D12Core/D3D12CommandContext.hpp>
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Core/D3D12Fence.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Resource.hpp>

#include <VarjoXR/VarjoXR.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Vdca {

struct StereoPlaneSurfaceDesc {
    // Stable application-owned source texture size. This must match the
    // synchronized MFFrameSource output frames.
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

    // Content size after optional Plane processing. Zero selects width/height.
    // This is used for the physical Plane aspect ratio.
    std::uint32_t contentWidth = 0;
    std::uint32_t contentHeight = 0;

    float planeWidthMeters = 1.0f;
    float planeDistanceMeters = 1.0f;
    float planeVerticalOffsetMeters = 0.0f;
    VarjoXR::PlacementMode placementMode = VarjoXR::PlacementMode::HeadRelative;
};

// Owns stable per-eye textures used by VarjoXR. Synchronized MFFrameSource
// textures are copied into these resources on the same D3D12 direct queue.
//
// Two XRTexture wrappers per eye alternate after every new synchronized frame.
// With ProcessingTiming::OnTextureChanged this causes one processing dispatch
// per eye and new camera frame, instead of one dispatch for every Varjo view.
class StereoPlaneSurface {
public:
    StereoPlaneSurface(
        std::shared_ptr<D3D12CoreLib::D3D12Core> core,
        VarjoXR::Backends::D3D12::D3D12Backend& backend,
        VarjoXR::XRSpace& space,
        const StereoPlaneSurfaceDesc& desc);

    ~StereoPlaneSurface();

    StereoPlaneSurface(const StereoPlaneSurface&) = delete;
    StereoPlaneSurface& operator=(const StereoPlaneSurface&) = delete;

    void updateFromSynchronizedFrame(MFFrameSource::MFD3D12StereoFrame& frame);

    VarjoXR::XRPlane& plane() noexcept { return *plane_; }
    const VarjoXR::XRPlane& plane() const noexcept { return *plane_; }

    void setProcessing(VarjoXR::Eye eye, const VarjoXR::TextureProcessingDesc& processing);

    std::uint64_t lastPairNumber() const noexcept { return lastPairNumber_; }
    std::int64_t lastAdjustedDiff100ns() const noexcept { return lastAdjustedDiff100ns_; }

private:
    static constexpr std::size_t kTextureVariantCount = 2;

    void validateFrame(const MFFrameSource::MFD3D12CameraFrame& frame, const char* eyeName) const;
    void waitForPreviousCopy();
    void activateNextTextureVariant();

    std::shared_ptr<D3D12CoreLib::D3D12Core> core_;
    D3D12CoreLib::D3D12CommandContext copyContext_;
    D3D12CoreLib::D3D12Fence copyFence_;
    D3D12CoreLib::D3D12Resource leftDisplayTexture_;
    D3D12CoreLib::D3D12Resource rightDisplayTexture_;
    std::array<std::shared_ptr<VarjoXR::Backends::D3D12::D3D12Texture>, kTextureVariantCount>
        leftXrTextures_{};
    std::array<std::shared_ptr<VarjoXR::Backends::D3D12::D3D12Texture>, kTextureVariantCount>
        rightXrTextures_{};
    std::size_t activeTextureVariant_ = 0;
    VarjoXR::XRPlane* plane_ = nullptr;

    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    UINT64 previousCopyFenceValue_ = 0;

    std::uint64_t lastPairNumber_ = 0;
    std::int64_t lastAdjustedDiff100ns_ = 0;
};

} // namespace Vdca
