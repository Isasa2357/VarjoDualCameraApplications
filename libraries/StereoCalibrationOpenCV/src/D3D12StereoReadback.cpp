#include "D3D12StereoReadback.hpp"

#include <D3D12Helper/D3D12Core/D3D12Barrier.hpp>

#include <opencv2/imgproc.hpp>

#include <Windows.h>

#include <cstddef>
#include <stdexcept>
#include <utility>

namespace Vdca::StereoCalibration::internal {
namespace {

StereoGpuReadback::Layout QueryLayout(
    ID3D12Device* device,
    std::uint32_t width,
    std::uint32_t height,
    DXGI_FORMAT format) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    StereoGpuReadback::Layout result;
    device->GetCopyableFootprints(
        &desc, 0, 1, 0,
        &result.footprint, &result.rows, &result.rowSize, &result.totalBytes);
    return result;
}

void WaitReady(const D3D12ReadyPoint& point) {
    if (!point.fence || point.value == 0 || point.fence->GetCompletedValue() >= point.value) return;
    HANDLE eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!eventHandle) throw std::runtime_error("CreateEventW failed while waiting for input frame");
    const HRESULT hr = point.fence->SetEventOnCompletion(point.value, eventHandle);
    if (FAILED(hr)) {
        CloseHandle(eventHandle);
        throw std::runtime_error("SetEventOnCompletion failed while waiting for input frame");
    }
    const DWORD waitResult = WaitForSingleObject(eventHandle, INFINITE);
    CloseHandle(eventHandle);
    if (waitResult != WAIT_OBJECT_0) throw std::runtime_error("WaitForSingleObject failed for input frame");
}

bool SupportedFormat(DXGI_FORMAT format) noexcept {
    return format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM;
}

} // namespace

StereoGpuReadback::StereoGpuReadback(std::shared_ptr<D3D12CoreLib::D3D12Core> core)
    : core_(std::move(core)) {
    if (!core_) throw std::invalid_argument("StereoGpuReadback: D3D12Core is null");
    context_ = core_->CreateDirectContext();
    fence_.Initialize(core_->GetDevice());
}

std::pair<cv::Mat, cv::Mat> StereoGpuReadback::readGray(const StereoD3D12Frame& frame) {
    validate(frame);
    ensureStorage(frame.left.width, frame.left.height, frame.left.format);
    WaitReady(frame.left.ready);
    WaitReady(frame.right.ready);

    context_.Reset();
    recordCopy(frame.left, leftReadback_);
    recordCopy(frame.right, rightReadback_);
    context_.Close();
    ID3D12CommandList* lists[] = {context_.GetCommandList()};
    core_->DirectQueue().ExecuteCommandLists(1, lists);
    const UINT64 value = fence_.Signal(core_->GetDirectCommandQueue());
    fence_.Wait(value);

    return {mapGray(leftReadback_, frame.left.format), mapGray(rightReadback_, frame.right.format)};
}

void StereoGpuReadback::validate(const StereoD3D12Frame& frame) const {
    if (!frame) throw std::invalid_argument("StereoGpuReadback: empty stereo frame");
    if (frame.left.width != frame.right.width || frame.left.height != frame.right.height ||
        frame.left.format != frame.right.format) {
        throw std::invalid_argument("StereoGpuReadback: left/right formats differ");
    }
    if (!SupportedFormat(frame.left.format)) {
        throw std::invalid_argument("StereoGpuReadback: only RGBA8 and BGRA8 are supported");
    }
}

void StereoGpuReadback::ensureStorage(std::uint32_t width, std::uint32_t height, DXGI_FORMAT format) {
    if (width_ == width && height_ == height && format_ == format) return;
    width_ = width;
    height_ = height;
    format_ = format;
    layout_ = QueryLayout(core_->GetDevice(), width, height, format);
    leftReadback_.Initialize(core_->GetDevice(), layout_.totalBytes);
    rightReadback_.Initialize(core_->GetDevice(), layout_.totalBytes);
}

void StereoGpuReadback::recordCopy(
    const D3D12ImageFrame& source,
    D3D12CoreLib::D3D12ReadbackBuffer& destination) {
    if (source.state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        context_.ResourceBarrier(D3D12CoreLib::MakeTransitionBarrier(
            source.resource.Get(), source.state, D3D12_RESOURCE_STATE_COPY_SOURCE));
    }

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = destination.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = layout_.footprint;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = source.resource.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    context_.GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    if (source.state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        context_.ResourceBarrier(D3D12CoreLib::MakeTransitionBarrier(
            source.resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, source.state));
    }
}

cv::Mat StereoGpuReadback::mapGray(
    D3D12CoreLib::D3D12ReadbackBuffer& buffer,
    DXGI_FORMAT format) {
    auto mapped = buffer.MapRead(layout_.footprint.Offset, layout_.totalBytes - layout_.footprint.Offset);
    if (!mapped) throw std::runtime_error("StereoGpuReadback: MapRead returned empty range");

    auto* base = const_cast<std::byte*>(mapped.Data());
    cv::Mat color(
        static_cast<int>(height_),
        static_cast<int>(width_),
        CV_8UC4,
        base,
        static_cast<std::size_t>(layout_.footprint.Footprint.RowPitch));

    cv::Mat gray;
    cv::cvtColor(
        color,
        gray,
        format == DXGI_FORMAT_R8G8B8A8_UNORM ? cv::COLOR_RGBA2GRAY : cv::COLOR_BGRA2GRAY);
    return gray;
}

} // namespace Vdca::StereoCalibration::internal
