#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace Vdca::StereoCalibration {

struct ImageSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    bool valid() const noexcept { return width != 0 && height != 0; }
};

struct Homography3x3 {
    std::array<double, 9> rows{
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        0.0, 0.0, 1.0,
    };
};

struct ProfileQuality {
    std::uint32_t usedPairs = 0;
    std::uint32_t usedPoints = 0;
    std::uint32_t inlierPoints = 0;
    std::optional<double> meanAbsVerticalErrorPx;
    std::optional<double> medianAbsVerticalErrorPx;
};

struct RectificationProfile {
    std::string method;
    nlohmann::json parameters = nlohmann::json::object();
    ProfileQuality quality;
    Homography3x3 leftForward;
    Homography3x3 leftInverse;
    Homography3x3 rightForward;
    Homography3x3 rightInverse;
};

struct CalibrationDocument {
    static constexpr int kVersion = 1;
    static constexpr const char* kFormat = "vdca.stereo_rectification";

    std::string defaultProfile = "uncalibrated";
    std::string generatorName = "RealtimeStereoCalibration";
    int generatorVersion = 1;

    ImageSize sourceSize;
    ImageSize calibrationInputSize;
    ImageSize rectifiedOutputSize;

    std::string resizeMode = "none";
    std::string rightOrder = "same";
    std::uint32_t boardColumns = 0;
    std::uint32_t boardRows = 0;

    std::string samplingFilter = "linear";
    std::string borderMode = "constant";
    std::array<float, 4> borderRgba{0.0f, 0.0f, 0.0f, 1.0f};

    std::map<std::string, RectificationProfile> profiles;

    const RectificationProfile& profile(const std::string& name) const;
    bool hasProfile(const std::string& name) const noexcept;

    nlohmann::json toJson() const;
    static CalibrationDocument fromJson(const nlohmann::json& json);
    static CalibrationDocument loadJson(const std::filesystem::path& path);
    void saveJsonAtomically(const std::filesystem::path& path, int indent = 2) const;
};

CalibrationDocument MakeIdentityCalibrationDocument(
    ImageSize sourceSize,
    ImageSize calibrationInputSize,
    ImageSize rectifiedOutputSize,
    std::uint32_t boardColumns,
    std::uint32_t boardRows,
    std::string rightOrder,
    std::string defaultProfile = "uncalibrated");

Homography3x3 InvertHomography(const Homography3x3& value);
void ValidateCalibrationDocument(const CalibrationDocument& document);

} // namespace Vdca::StereoCalibration
