#pragma once

#include <VdcaStereoCalibration/D3D12StereoFrame.hpp>

#include <D3D12Helper/D3D12Core/D3D12CommandContext.hpp>
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Core/D3D12Fence.hpp>
#include <D3D12Helper/D3D12Framework/D3D12ReadbackBuffer.hpp>

#include <opencv2/core.hpp>

#include <cstdint>
#include <memory>
#include <utility>

namespace Vdca::StereoCalibration::internal {

class StereoGpuReadback {
public:
    struct Layout {
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT rows = 0;
        UINT64 rowSize = 0;
        UINT64 totalBytes = 0;
    };

    explicit StereoGpuReadback(std::shared_ptr<D3D12CoreLib::D3D12Core> core);
    std::pair<cv::Mat, cv::Mat> readGray(const StereoD3D12Frame& frame);

private:
    void validate(const StereoD3D12Frame& frame) const;
    void ensureStorage(std::uint32_t width, std::uint32_t height, DXGI_FORMAT format);
    void recordCopy(const D3D12ImageFrame& source, D3D12CoreLib::D3D12ReadbackBuffer& destination);
    cv::Mat mapGray(D3D12CoreLib::D3D12ReadbackBuffer& buffer, DXGI_FORMAT format);

    std::shared_ptr<D3D12CoreLib::D3D12Core> core_;
    D3D12CoreLib::D3D12CommandContext context_;
    D3D12CoreLib::D3D12Fence fence_;
    D3D12CoreLib::D3D12ReadbackBuffer leftReadback_;
    D3D12CoreLib::D3D12ReadbackBuffer rightReadback_;
    Layout layout_{};
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
};

} // namespace Vdca::StereoCalibration::internal
