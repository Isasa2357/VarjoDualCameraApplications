#include <VdcaVarjoStereoView/VarjoStereoView.hpp>

#include <D3D12Helper/D3D12Gpu/D3D12Copy.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Helpers.hpp>

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace Vdca::VarjoStereoView {
namespace {

using StereoCalibration::Homography3x3;
using StereoCalibration::ImageSize;
using StereoCalibration::RectificationProfile;

bool SupportedFormat(DXGI_FORMAT format) noexcept {
    return format == DXGI_FORMAT_R8G8B8A8_UNORM ||
           format == DXGI_FORMAT_B8G8R8A8_UNORM;
}

struct alignas(16) RectificationConstants {
    std::array<float, 4> inverseRow0{};
    std::array<float, 4> inverseRow1{};
    std::array<float, 4> inverseRow2{};
    std::array<float, 4> borderRgba{};
};

static_assert(sizeof(RectificationConstants) == 64, "Unexpected rectification constant-buffer layout");

float CheckedFloat(double value) {
    const float result = static_cast<float>(value);
    if (!std::isfinite(result)) throw std::invalid_argument("calibration matrix contains a non-finite float");
    return result;
}

RectificationConstants MakeConstants(
    const Homography3x3& inverse,
    const std::array<float, 4>& borderRgba) {
    RectificationConstants result;
    for (std::size_t c = 0; c < 3; ++c) {
        result.inverseRow0[c] = CheckedFloat(inverse.rows[c]);
        result.inverseRow1[c] = CheckedFloat(inverse.rows[3 + c]);
        result.inverseRow2[c] = CheckedFloat(inverse.rows[6 + c]);
    }
    result.borderRgba = borderRgba;
    return result;
}

const char* RemapHlsl() noexcept {
    return R"hlsl(
Texture2D<float4> xrInput : register(t0);
RWTexture2D<float4> xrOutput : register(u0);

cbuffer RectificationConstants : register(b0)
{
    float4 inverseRow0;
    float4 inverseRow1;
    float4 inverseRow2;
    float4 borderRgba;
};

cbuffer XRTextureProcessingFrameConstants : register(b1)
{
    uint srcWidth;
    uint srcHeight;
    uint dstWidth;
    uint dstHeight;
    float4 frameParams;
};

float4 LoadWithConstantBorder(int2 pixel)
{
    if (pixel.x < 0 || pixel.y < 0 || pixel.x >= (int)srcWidth || pixel.y >= (int)srcHeight) {
        return borderRgba;
    }
    return xrInput.Load(int3(pixel, 0));
}

float4 SampleLinear(float2 sourcePixel)
{
    const float2 baseFloat = floor(sourcePixel);
    const int2 basePixel = int2(baseFloat);
    const float2 fraction = sourcePixel - baseFloat;
    const float4 c00 = LoadWithConstantBorder(basePixel);
    const float4 c10 = LoadWithConstantBorder(basePixel + int2(1, 0));
    const float4 c01 = LoadWithConstantBorder(basePixel + int2(0, 1));
    const float4 c11 = LoadWithConstantBorder(basePixel + int2(1, 1));
    return lerp(lerp(c00, c10, fraction.x), lerp(c01, c11, fraction.x), fraction.y);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= dstWidth || id.y >= dstHeight) return;
    const float3 destinationPixel = float3((float)id.x, (float)id.y, 1.0f);
    const float3 sourceH = float3(
        dot(inverseRow0.xyz, destinationPixel),
        dot(inverseRow1.xyz, destinationPixel),
        dot(inverseRow2.xyz, destinationPixel));
    if (!isfinite(sourceH.z) || abs(sourceH.z) < 1.0e-8f) {
        xrOutput[id.xy] = borderRgba;
        return;
    }
    const float2 sourcePixel = sourceH.xy / sourceH.z;
    if (!all(isfinite(sourcePixel)) ||
        sourcePixel.x <= -1.0f || sourcePixel.y <= -1.0f ||
        sourcePixel.x >= (float)srcWidth || sourcePixel.y >= (float)srcHeight) {
        xrOutput[id.xy] = borderRgba;
        return;
    }
    xrOutput[id.xy] = SampleLinear(sourcePixel);
}
)hlsl";
}

VarjoXR::TextureProcessingDesc MakeProcessing(
    const Homography3x3& inverse,
    const std::array<float, 4>& borderRgba,
    ImageSize outputSize) {
    VarjoXR::TextureProcessingDesc result{};
    result.enabled = true;
    result.timing = VarjoXR::ProcessingTiming::OnTextureChanged;
    result.hlsl = RemapHlsl();
    result.entryPoint = "main";
    result.target = "cs_5_0";
    result.sourceName = "VdcaVarjoStereoView_Remap.hlsl";
    result.outputSize = {outputSize.width, outputSize.height};
    result.userConstants.registerIndex = 0;
    result.userConstants.set(MakeConstants(inverse, borderRgba));
    result.frameConstants.enabled = true;
    result.frameConstants.registerIndex = 1;
    return result;
}

void ValidateResourceShape(
    const StereoCalibration::D3D12ImageFrame& source,
    const D3D12CoreLib::D3D12Resource& destination,
    const char* eye) {
    const auto src = source.resource->GetDesc();
    const auto dst = destination.GetDesc();
    if (src.Dimension != dst.Dimension || src.Width != dst.Width || src.Height != dst.Height ||
        src.DepthOrArraySize != dst.DepthOrArraySize || src.MipLevels != dst.MipLevels ||
        src.Format != dst.Format || src.SampleDesc.Count != dst.SampleDesc.Count ||
        src.SampleDesc.Quality != dst.SampleDesc.Quality) {
        throw std::invalid_argument(std::string("VarjoStereoView: ") + eye + " resource is not copy-compatible");
    }
}

} // namespace

VarjoStereoView::VarjoStereoView(
    std::shared_ptr<D3D12CoreLib::D3D12Core> core,
    VarjoXR::Backends::D3D12::D3D12Backend& backend,
    VarjoXR::XRSpace& space,
    ViewConfig config)
    : core_(std::move(core)), config_(config) {
    if (!core_) throw std::invalid_argument("VarjoStereoView: D3D12Core is null");
    if (!config_.inputSize.valid() || !config_.rectifiedOutputSize.valid()) {
        throw std::invalid_argument("VarjoStereoView: image sizes must be non-zero");
    }
    if (!SupportedFormat(config_.format)) throw std::invalid_argument("VarjoStereoView: unsupported format");
    if (!(config_.planeWidthMeters > 0.0f) || !(config_.planeDistanceMeters > 0.0f)) {
        throw std::invalid_argument("VarjoStereoView: plane width and distance must be positive");
    }

    copyContext_ = core_->CreateDirectContext();
    copyFence_.Initialize(core_->GetDevice());
    leftTexture_ = D3D12CoreLib::CreateTexture2D(
        *core_, config_.inputSize.width, config_.inputSize.height, config_.format,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    rightTexture_ = D3D12CoreLib::CreateTexture2D(
        *core_, config_.inputSize.width, config_.inputSize.height, config_.format,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    for (std::size_t i = 0; i < 2; ++i) {
        leftAliases_[i] = backend.wrapResource(leftTexture_.Get(), config_.format);
        rightAliases_[i] = backend.wrapResource(rightTexture_.Get(), config_.format);
    }

    const float aspect = static_cast<float>(config_.rectifiedOutputSize.height) /
                         static_cast<float>(config_.rectifiedOutputSize.width);
    plane_ = &space.createPlane({config_.planeWidthMeters, config_.planeWidthMeters * aspect});
    plane_->setPlacementMode(config_.placementMode);
    plane_->transform().position = {
        0.0f,
        config_.planeVerticalOffsetMeters,
        -config_.planeDistanceMeters,
    };
    plane_->setTexture(VarjoXR::Eye::Left, leftAliases_[aliasIndex_]);
    plane_->setTexture(VarjoXR::Eye::Right, rightAliases_[aliasIndex_]);
}

VarjoStereoView::~VarjoStereoView() {
    try { waitForPreviousCopy(); } catch (...) {}
}

void VarjoStereoView::validateFrame(const StereoCalibration::StereoD3D12Frame& frame) const {
    if (!frame) throw std::invalid_argument("VarjoStereoView::submitFrame: empty frame");
    if (frame.left.width != config_.inputSize.width || frame.left.height != config_.inputSize.height ||
        frame.right.width != config_.inputSize.width || frame.right.height != config_.inputSize.height ||
        frame.left.format != config_.format || frame.right.format != config_.format) {
        throw std::invalid_argument("VarjoStereoView::submitFrame: frame size or format mismatch");
    }
    ValidateResourceShape(frame.left, leftTexture_, "left");
    ValidateResourceShape(frame.right, rightTexture_, "right");
}

void VarjoStereoView::waitForPreviousCopy() {
    if (copyFenceValue_ != 0) {
        copyFence_.Wait(copyFenceValue_);
        copyFenceValue_ = 0;
        inFlightLifetimeToken_.reset();
    }
}

void VarjoStereoView::submitFrame(StereoCalibration::StereoD3D12Frame frame) {
    validateFrame(frame);
    waitForPreviousCopy();
    copyContext_.Reset();

    D3D12CoreLib::D3D12Resource leftSource(frame.left.resource, frame.left.state);
    D3D12CoreLib::D3D12Resource rightSource(frame.right.resource, frame.right.state);
    D3D12CoreLib::RecordCopyResource(
        copyContext_, leftTexture_, leftSource,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, frame.left.state);
    D3D12CoreLib::RecordCopyResource(
        copyContext_, rightTexture_, rightSource,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, frame.right.state);

    copyContext_.Close();
    ID3D12CommandList* lists[] = {copyContext_.GetCommandList()};
    core_->DirectQueue().ExecuteCommandLists(1, lists);
    copyFenceValue_ = copyFence_.Signal(core_->GetDirectCommandQueue());
    inFlightLifetimeToken_ = std::move(frame.lifetimeToken);
    lastFrameNumber_ = frame.frameNumber;

    aliasIndex_ = (aliasIndex_ + 1u) % leftAliases_.size();
    plane_->setTexture(VarjoXR::Eye::Left, leftAliases_[aliasIndex_]);
    plane_->setTexture(VarjoXR::Eye::Right, rightAliases_[aliasIndex_]);
}

void VarjoStereoView::applyCalibration(const StereoCalibration::CalibrationSnapshot& snapshot) {
    if (!snapshot || !snapshot.document) throw std::invalid_argument("VarjoStereoView::applyCalibration: empty snapshot");
    const auto& document = *snapshot.document;
    if (document.calibrationInputSize.width != config_.inputSize.width ||
        document.calibrationInputSize.height != config_.inputSize.height ||
        document.rectifiedOutputSize.width != config_.rectifiedOutputSize.width ||
        document.rectifiedOutputSize.height != config_.rectifiedOutputSize.height) {
        throw std::invalid_argument("VarjoStereoView::applyCalibration: calibration geometry does not match the view");
    }
    const RectificationProfile& profile = document.profile(snapshot.activeProfile);
    plane_->setProcessing(
        VarjoXR::Eye::Left,
        MakeProcessing(profile.leftInverse, document.borderRgba, document.rectifiedOutputSize));
    plane_->setProcessing(
        VarjoXR::Eye::Right,
        MakeProcessing(profile.rightInverse, document.borderRgba, document.rectifiedOutputSize));
    appliedRevision_ = snapshot.revision;
}

void VarjoStereoView::clearCalibration() {
    VarjoXR::TextureProcessingDesc disabled{};
    plane_->setProcessing(VarjoXR::Eye::Left, disabled);
    plane_->setProcessing(VarjoXR::Eye::Right, disabled);
    appliedRevision_ = 0;
}

} // namespace Vdca::VarjoStereoView
