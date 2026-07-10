#include <VdcaStereoCalibration/D3D12StereoFrame.hpp>

#include <stdexcept>
#include <utility>

namespace Vdca::StereoCalibration {

D3D12ImageFrame MakeD3D12ImageFrame(
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES state,
    D3D12ReadyPoint ready) {
    if (!resource) throw std::invalid_argument("MakeD3D12ImageFrame: resource is null");
    const auto desc = resource->GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        desc.DepthOrArraySize != 1 || desc.MipLevels != 1 || desc.SampleDesc.Count != 1) {
        throw std::invalid_argument("MakeD3D12ImageFrame: only non-MSAA Texture2D mip0 array0 is supported");
    }

    D3D12ImageFrame result;
    result.resource = resource;
    result.state = state;
    result.format = desc.Format;
    result.width = static_cast<std::uint32_t>(desc.Width);
    result.height = desc.Height;
    result.ready = std::move(ready);
    return result;
}

} // namespace Vdca::StereoCalibration
