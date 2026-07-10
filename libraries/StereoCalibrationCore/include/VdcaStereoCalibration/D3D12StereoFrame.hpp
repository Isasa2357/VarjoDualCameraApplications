#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgiformat.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>

namespace Vdca::StereoCalibration {

struct D3D12ReadyPoint {
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    std::uint64_t value = 0;
};

struct D3D12ImageFrame {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    D3D12ReadyPoint ready;

    explicit operator bool() const noexcept {
        return resource && width != 0 && height != 0 && format != DXGI_FORMAT_UNKNOWN;
    }
};

struct StereoD3D12Frame {
    D3D12ImageFrame left;
    D3D12ImageFrame right;

    std::uint64_t frameNumber = 0;
    std::int64_t leftTimestamp100ns = 0;
    std::int64_t rightTimestamp100ns = 0;

    // Optional owner supplied by the producer. It remains alive until analysis
    // has completed its GPU readback, which prevents pool-backed resources from
    // being recycled while the frame is still in use.
    std::shared_ptr<void> lifetimeToken;

    explicit operator bool() const noexcept {
        return static_cast<bool>(left) && static_cast<bool>(right);
    }
};

D3D12ImageFrame MakeD3D12ImageFrame(
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES state,
    D3D12ReadyPoint ready = {});

} // namespace Vdca::StereoCalibration
