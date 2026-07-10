#include "StereoRectification.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace Vdca {
namespace {

using Json = nlohmann::json;

[[noreturn]] void ThrowSchemaError(const std::string& location, const std::string& message) {
    throw std::runtime_error("stereo rectification JSON " + location + ": " + message);
}

const Json& RequireObjectMember(const Json& object, const char* key, const std::string& location) {
    if (!object.is_object()) {
        ThrowSchemaError(location, "expected an object");
    }
    const auto it = object.find(key);
    if (it == object.end()) {
        ThrowSchemaError(location, std::string("missing required member '") + key + "'");
    }
    if (!it->is_object()) {
        ThrowSchemaError(location + "." + key, "expected an object");
    }
    return *it;
}

const Json& RequireArrayMember(const Json& object, const char* key, const std::string& location) {
    if (!object.is_object()) {
        ThrowSchemaError(location, "expected an object");
    }
    const auto it = object.find(key);
    if (it == object.end()) {
        ThrowSchemaError(location, std::string("missing required member '") + key + "'");
    }
    if (!it->is_array()) {
        ThrowSchemaError(location + "." + key, "expected an array");
    }
    return *it;
}

std::string RequireStringMember(const Json& object, const char* key, const std::string& location) {
    if (!object.is_object()) {
        ThrowSchemaError(location, "expected an object");
    }
    const auto it = object.find(key);
    if (it == object.end()) {
        ThrowSchemaError(location, std::string("missing required member '") + key + "'");
    }
    if (!it->is_string()) {
        ThrowSchemaError(location + "." + key, "expected a string");
    }
    return it->get<std::string>();
}

std::uint32_t RequireU32Member(const Json& object, const char* key, const std::string& location) {
    if (!object.is_object()) {
        ThrowSchemaError(location, "expected an object");
    }
    const auto it = object.find(key);
    if (it == object.end()) {
        ThrowSchemaError(location, std::string("missing required member '") + key + "'");
    }
    if (!it->is_number_unsigned() && !it->is_number_integer()) {
        ThrowSchemaError(location + "." + key, "expected a positive integer");
    }
    const auto value = it->get<long long>();
    if (value <= 0 || static_cast<unsigned long long>(value) > std::numeric_limits<std::uint32_t>::max()) {
        ThrowSchemaError(location + "." + key, "value is outside the supported positive uint32 range");
    }
    return static_cast<std::uint32_t>(value);
}

int RequireIntMember(const Json& object, const char* key, const std::string& location) {
    if (!object.is_object()) {
        ThrowSchemaError(location, "expected an object");
    }
    const auto it = object.find(key);
    if (it == object.end()) {
        ThrowSchemaError(location, std::string("missing required member '") + key + "'");
    }
    if (!it->is_number_integer()) {
        ThrowSchemaError(location + "." + key, "expected an integer");
    }
    return it->get<int>();
}

double RequireFiniteNumber(const Json& value, const std::string& location) {
    if (!value.is_number()) {
        ThrowSchemaError(location, "expected a number");
    }
    const double result = value.get<double>();
    if (!std::isfinite(result)) {
        ThrowSchemaError(location, "number must be finite");
    }
    return result;
}

RectificationImageSize ParseImageSize(const Json& object, const std::string& location) {
    RectificationImageSize size;
    size.width = RequireU32Member(object, "width", location);
    size.height = RequireU32Member(object, "height", location);
    return size;
}

std::array<double, 9> ParseHomography(const Json& object, const std::string& location) {
    const auto& rows = RequireArrayMember(object, "rows", location);
    if (rows.size() != 3) {
        ThrowSchemaError(location + ".rows", "expected exactly 3 rows");
    }

    std::array<double, 9> result{};
    for (std::size_t row = 0; row < 3; ++row) {
        if (!rows[row].is_array() || rows[row].size() != 3) {
            ThrowSchemaError(location + ".rows[" + std::to_string(row) + "]", "expected exactly 3 numbers");
        }
        for (std::size_t column = 0; column < 3; ++column) {
            result[row * 3 + column] = RequireFiniteNumber(
                rows[row][column],
                location + ".rows[" + std::to_string(row) + "][" + std::to_string(column) + "]");
        }
    }
    return result;
}

std::array<double, 9> MultiplyHomographies(
    const std::array<double, 9>& a,
    const std::array<double, 9>& b) {
    std::array<double, 9> result{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            double value = 0.0;
            for (std::size_t k = 0; k < 3; ++k) {
                value += a[row * 3 + k] * b[k * 3 + column];
            }
            result[row * 3 + column] = value;
        }
    }
    return result;
}

void ValidateIdentityUpToScale(const std::array<double, 9>& matrix, const std::string& location) {
    const double scale = (matrix[0] + matrix[4] + matrix[8]) / 3.0;
    if (!std::isfinite(scale) || std::abs(scale) < 1.0e-12) {
        ThrowSchemaError(location, "forward and inverse matrices do not form an invertible homography pair");
    }

    const double tolerance = std::max(1.0, std::abs(scale)) * 1.0e-3;
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            const double expected = row == column ? scale : 0.0;
            if (std::abs(matrix[row * 3 + column] - expected) > tolerance) {
                ThrowSchemaError(location, "forward and inverse matrices are inconsistent");
            }
        }
    }
}

RectificationEyeTransform ParseEyeTransform(const Json& eye, const std::string& location) {
    RectificationEyeTransform result;
    result.forwardPixelHomography = ParseHomography(
        RequireObjectMember(eye, "forward_pixel_homography", location),
        location + ".forward_pixel_homography");
    result.inversePixelHomography = ParseHomography(
        RequireObjectMember(eye, "inverse_pixel_homography", location),
        location + ".inverse_pixel_homography");

    ValidateIdentityUpToScale(
        MultiplyHomographies(result.forwardPixelHomography, result.inversePixelHomography),
        location + " forward*inverse");
    ValidateIdentityUpToScale(
        MultiplyHomographies(result.inversePixelHomography, result.forwardPixelHomography),
        location + " inverse*forward");
    return result;
}

void RequireExactString(
    const Json& object,
    const char* key,
    const char* expected,
    const std::string& location) {
    const auto actual = RequireStringMember(object, key, location);
    if (actual != expected) {
        ThrowSchemaError(
            location + "." + key,
            "expected '" + std::string(expected) + "' but found '" + actual + "'");
    }
}

bool IsSupportedMethod(const std::string& method) noexcept {
    return method == "uncalibrated" ||
           method == "affine_vertical" ||
           method == "affine_full";
}

float CheckedFloat(double value, const std::string& location) {
    if (value < -std::numeric_limits<float>::max() || value > std::numeric_limits<float>::max()) {
        ThrowSchemaError(location, "number is outside the float range required by HLSL");
    }
    const float result = static_cast<float>(value);
    if (!std::isfinite(result)) {
        ThrowSchemaError(location, "number cannot be represented as a finite HLSL float");
    }
    return result;
}

struct alignas(16) RectificationConstants {
    std::array<float, 4> inverseRow0{};
    std::array<float, 4> inverseRow1{};
    std::array<float, 4> inverseRow2{};
    std::array<float, 4> borderRgba{};
};

static_assert(sizeof(RectificationConstants) == 64, "Unexpected rectification constant-buffer layout");

RectificationConstants MakeConstants(
    const RectificationEyeTransform& eye,
    const std::array<float, 4>& borderRgba) {
    RectificationConstants constants;
    for (std::size_t column = 0; column < 3; ++column) {
        constants.inverseRow0[column] = CheckedFloat(
            eye.inversePixelHomography[column],
            "inverse_pixel_homography row 0");
        constants.inverseRow1[column] = CheckedFloat(
            eye.inversePixelHomography[3 + column],
            "inverse_pixel_homography row 1");
        constants.inverseRow2[column] = CheckedFloat(
            eye.inversePixelHomography[6 + column],
            "inverse_pixel_homography row 2");
    }
    constants.borderRgba = borderRgba;
    return constants;
}

const char* RectificationComputeHlsl() noexcept {
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
    if (pixel.x < 0 || pixel.y < 0 ||
        pixel.x >= (int)srcWidth || pixel.y >= (int)srcHeight) {
        return borderRgba;
    }
    return xrInput.Load(int3(pixel, 0));
}

float4 SampleLinearAtPixelCoordinate(float2 sourcePixel)
{
    const float2 baseFloat = floor(sourcePixel);
    const int2 basePixel = int2(baseFloat);
    const float2 fraction = sourcePixel - baseFloat;

    const float4 c00 = LoadWithConstantBorder(basePixel);
    const float4 c10 = LoadWithConstantBorder(basePixel + int2(1, 0));
    const float4 c01 = LoadWithConstantBorder(basePixel + int2(0, 1));
    const float4 c11 = LoadWithConstantBorder(basePixel + int2(1, 1));

    const float4 top = lerp(c00, c10, fraction.x);
    const float4 bottom = lerp(c01, c11, fraction.x);
    return lerp(top, bottom, fraction.y);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= dstWidth || id.y >= dstHeight) {
        return;
    }

    // The JSON coordinate convention says integer coordinates are pixel centers.
    // Therefore output pixel id.xy is transformed directly, without adding 0.5.
    const float3 destinationPixel = float3((float)id.x, (float)id.y, 1.0f);
    const float3 sourceHomogeneous = float3(
        dot(inverseRow0.xyz, destinationPixel),
        dot(inverseRow1.xyz, destinationPixel),
        dot(inverseRow2.xyz, destinationPixel));

    if (!isfinite(sourceHomogeneous.z) || abs(sourceHomogeneous.z) < 1.0e-8f) {
        xrOutput[id.xy] = borderRgba;
        return;
    }

    const float2 sourcePixel = sourceHomogeneous.xy / sourceHomogeneous.z;
    if (!all(isfinite(sourcePixel)) ||
        sourcePixel.x <= -1.0f || sourcePixel.y <= -1.0f ||
        sourcePixel.x >= (float)srcWidth || sourcePixel.y >= (float)srcHeight) {
        xrOutput[id.xy] = borderRgba;
        return;
    }

    xrOutput[id.xy] = SampleLinearAtPixelCoordinate(sourcePixel);
}
)hlsl";
}

std::string SizeText(const RectificationImageSize& size) {
    return std::to_string(size.width) + "x" + std::to_string(size.height);
}

} // namespace

StereoRectificationProfile LoadStereoRectificationProfile(
    const std::filesystem::path& path,
    const std::string& requestedProfile) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("failed to open stereo rectification JSON: " + path.string());
    }

    Json root;
    try {
        stream >> root;
    } catch (const Json::exception& error) {
        throw std::runtime_error(
            "failed to parse stereo rectification JSON '" + path.string() + "': " + error.what());
    }

    if (!root.is_object()) {
        ThrowSchemaError("root", "expected an object");
    }
    RequireExactString(root, "format", "vdca.stereo_rectification", "root");
    const int version = RequireIntMember(root, "version", "root");
    if (version != 1) {
        ThrowSchemaError("root.version", "only version 1 is supported");
    }

    const std::string profileName = requestedProfile.empty()
        ? RequireStringMember(root, "default_profile", "root")
        : requestedProfile;
    if (profileName.empty()) {
        ThrowSchemaError("root.default_profile", "profile name must not be empty");
    }

    const auto& imageGeometry = RequireObjectMember(root, "image_geometry", "root");
    const auto sourceSize = ParseImageSize(
        RequireObjectMember(imageGeometry, "source_size", "root.image_geometry"),
        "root.image_geometry.source_size");
    const auto calibrationInputSize = ParseImageSize(
        RequireObjectMember(imageGeometry, "calibration_input_size", "root.image_geometry"),
        "root.image_geometry.calibration_input_size");
    const auto rectifiedOutputSize = ParseImageSize(
        RequireObjectMember(imageGeometry, "rectified_output_size", "root.image_geometry"),
        "root.image_geometry.rectified_output_size");

    const auto& coordinateSystem = RequireObjectMember(root, "coordinate_system", "root");
    RequireExactString(coordinateSystem, "origin", "top_left", "root.coordinate_system");
    RequireExactString(coordinateSystem, "x_axis", "right", "root.coordinate_system");
    RequireExactString(coordinateSystem, "y_axis", "down", "root.coordinate_system");
    RequireExactString(
        coordinateSystem,
        "pixel_coordinate",
        "integer_is_pixel_center",
        "root.coordinate_system");
    RequireExactString(
        coordinateSystem,
        "forward_mapping",
        "rectified_pixel = H * source_pixel",
        "root.coordinate_system");

    const auto& sampling = RequireObjectMember(root, "sampling", "root");
    RequireExactString(sampling, "filter", "linear", "root.sampling");
    RequireExactString(sampling, "border_mode", "constant", "root.sampling");
    const auto& border = RequireArrayMember(sampling, "border_rgba", "root.sampling");
    if (border.size() != 4) {
        ThrowSchemaError("root.sampling.border_rgba", "expected exactly 4 normalized RGBA values");
    }

    std::array<float, 4> borderRgba{};
    for (std::size_t i = 0; i < border.size(); ++i) {
        const double value = RequireFiniteNumber(
            border[i],
            "root.sampling.border_rgba[" + std::to_string(i) + "]");
        if (value < 0.0 || value > 1.0) {
            ThrowSchemaError(
                "root.sampling.border_rgba[" + std::to_string(i) + "]",
                "expected a normalized value in [0, 1]");
        }
        borderRgba[i] = static_cast<float>(value);
    }

    const auto& profiles = RequireObjectMember(root, "profiles", "root");
    const auto profileIt = profiles.find(profileName);
    if (profileIt == profiles.end()) {
        ThrowSchemaError("root.profiles", "selected profile '" + profileName + "' does not exist");
    }
    if (!profileIt->is_object()) {
        ThrowSchemaError("root.profiles." + profileName, "expected an object");
    }

    const auto profileLocation = "root.profiles." + profileName;
    const std::string method = RequireStringMember(*profileIt, "method", profileLocation);
    if (!IsSupportedMethod(method)) {
        ThrowSchemaError(
            profileLocation + ".method",
            "expected uncalibrated, affine_vertical, or affine_full");
    }

    const auto& eyes = RequireObjectMember(*profileIt, "eyes", profileLocation);

    StereoRectificationProfile result;
    result.profileName = profileName;
    result.method = method;
    result.sourceSize = sourceSize;
    result.calibrationInputSize = calibrationInputSize;
    result.rectifiedOutputSize = rectifiedOutputSize;
    result.borderRgba = borderRgba;
    result.left = ParseEyeTransform(
        RequireObjectMember(eyes, "left", profileLocation + ".eyes"),
        profileLocation + ".eyes.left");
    result.right = ParseEyeTransform(
        RequireObjectMember(eyes, "right", profileLocation + ".eyes"),
        profileLocation + ".eyes.right");
    return result;
}

void StereoRectificationProfile::validateCameraInputSize(
    std::uint32_t width,
    std::uint32_t height) const {
    if (width != sourceSize.width || height != sourceSize.height) {
        throw std::runtime_error(
            "camera native input size " + std::to_string(width) + "x" + std::to_string(height) +
            " does not match rectification source_size " + SizeText(sourceSize));
    }
}

void StereoRectificationProfile::validateProcessingInputSize(
    std::uint32_t width,
    std::uint32_t height) const {
    if (width != calibrationInputSize.width || height != calibrationInputSize.height) {
        throw std::runtime_error(
            "MFFrameSource output size " + std::to_string(width) + "x" + std::to_string(height) +
            " does not match rectification calibration_input_size " + SizeText(calibrationInputSize));
    }
}

VarjoXR::TextureProcessingDesc StereoRectificationProfile::makeProcessing(VarjoXR::Eye eye) const {
    const auto& transform = eye == VarjoXR::Eye::Left ? left : right;
    const auto constants = MakeConstants(transform, borderRgba);

    VarjoXR::TextureProcessingDesc processing{};
    processing.enabled = true;
    processing.timing = VarjoXR::ProcessingTiming::BeforeRenderEachFrame;
    processing.hlsl = RectificationComputeHlsl();
    processing.entryPoint = "main";
    processing.target = "cs_5_0";
    processing.sourceName = "MFFrameSyncToVarjoPlane_Rectification.hlsl";
    processing.outputSize = {
        rectifiedOutputSize.width,
        rectifiedOutputSize.height,
    };
    processing.userConstants.registerIndex = 0;
    processing.userConstants.set(constants);
    processing.frameConstants.enabled = true;
    processing.frameConstants.registerIndex = 1;
    return processing;
}

} // namespace Vdca
