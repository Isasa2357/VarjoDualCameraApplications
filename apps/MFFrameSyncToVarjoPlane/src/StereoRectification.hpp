#pragma once

#include <VarjoXR/VarjoXR.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace Vdca {

struct RectificationImageSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct RectificationEyeTransform {
    std::array<double, 9> forwardPixelHomography{};
    std::array<double, 9> inversePixelHomography{};
};

struct StereoRectificationProfile {
    std::string profileName;
    std::string method;

    RectificationImageSize sourceSize;
    RectificationImageSize calibrationInputSize;
    RectificationImageSize rectifiedOutputSize;

    RectificationEyeTransform left;
    RectificationEyeTransform right;

    std::array<float, 4> borderRgba{0.0f, 0.0f, 0.0f, 1.0f};

    void validateCameraInputSize(std::uint32_t width, std::uint32_t height) const;
    void validateProcessingInputSize(std::uint32_t width, std::uint32_t height) const;

    VarjoXR::TextureProcessingDesc makeProcessing(VarjoXR::Eye eye) const;
};

// Loads the agreed vdca.stereo_rectification version 1 JSON format.
// requestedProfile may be empty, in which case default_profile is selected.
StereoRectificationProfile LoadStereoRectificationProfile(
    const std::filesystem::path& path,
    const std::string& requestedProfile = {});

} // namespace Vdca
