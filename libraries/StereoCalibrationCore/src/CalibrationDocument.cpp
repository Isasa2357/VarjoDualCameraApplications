#include <VdcaStereoCalibration/CalibrationDocument.hpp>

#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace Vdca::StereoCalibration {
namespace {

using Json = nlohmann::json;

[[noreturn]] void SchemaError(const std::string& location, const std::string& message) {
    throw std::runtime_error("stereo rectification JSON " + location + ": " + message);
}

const Json& RequireObject(const Json& parent, const char* key, const std::string& location) {
    const auto it = parent.find(key);
    if (it == parent.end()) SchemaError(location, std::string("missing '") + key + "'");
    if (!it->is_object()) SchemaError(location + "." + key, "expected object");
    return *it;
}

const Json& RequireArray(const Json& parent, const char* key, const std::string& location) {
    const auto it = parent.find(key);
    if (it == parent.end()) SchemaError(location, std::string("missing '") + key + "'");
    if (!it->is_array()) SchemaError(location + "." + key, "expected array");
    return *it;
}

std::string RequireString(const Json& parent, const char* key, const std::string& location) {
    const auto it = parent.find(key);
    if (it == parent.end()) SchemaError(location, std::string("missing '") + key + "'");
    if (!it->is_string()) SchemaError(location + "." + key, "expected string");
    return it->get<std::string>();
}

std::uint32_t RequireU32(const Json& parent, const char* key, const std::string& location) {
    const auto it = parent.find(key);
    if (it == parent.end()) SchemaError(location, std::string("missing '") + key + "'");
    if (!it->is_number_integer() && !it->is_number_unsigned()) {
        SchemaError(location + "." + key, "expected positive integer");
    }
    const auto value = it->get<long long>();
    if (value <= 0 || static_cast<unsigned long long>(value) > std::numeric_limits<std::uint32_t>::max()) {
        SchemaError(location + "." + key, "positive integer is outside uint32 range");
    }
    return static_cast<std::uint32_t>(value);
}

int RequireInt(const Json& parent, const char* key, const std::string& location) {
    const auto it = parent.find(key);
    if (it == parent.end()) SchemaError(location, std::string("missing '") + key + "'");
    if (!it->is_number_integer()) SchemaError(location + "." + key, "expected integer");
    return it->get<int>();
}

double RequireFinite(const Json& value, const std::string& location) {
    if (!value.is_number()) SchemaError(location, "expected number");
    const double result = value.get<double>();
    if (!std::isfinite(result)) SchemaError(location, "number must be finite");
    return result;
}

std::optional<double> OptionalFinite(const Json& parent, const char* key, const std::string& location) {
    const auto it = parent.find(key);
    if (it == parent.end() || it->is_null()) return std::nullopt;
    return RequireFinite(*it, location + "." + key);
}

std::uint32_t OptionalU32(const Json& parent, const char* key, std::uint32_t fallback, const std::string& location) {
    const auto it = parent.find(key);
    if (it == parent.end()) return fallback;
    if (!it->is_number_integer() && !it->is_number_unsigned()) {
        SchemaError(location + "." + key, "expected non-negative integer");
    }
    const auto value = it->get<long long>();
    if (value < 0 || static_cast<unsigned long long>(value) > std::numeric_limits<std::uint32_t>::max()) {
        SchemaError(location + "." + key, "integer is outside uint32 range");
    }
    return static_cast<std::uint32_t>(value);
}

ImageSize ParseSize(const Json& object, const std::string& location) {
    return {RequireU32(object, "width", location), RequireU32(object, "height", location)};
}

Json SizeJson(ImageSize size) {
    return Json{{"width", size.width}, {"height", size.height}};
}

Homography3x3 ParseHomography(const Json& object, const std::string& location) {
    const auto& rows = RequireArray(object, "rows", location);
    if (rows.size() != 3) SchemaError(location + ".rows", "expected three rows");
    Homography3x3 result;
    for (std::size_t r = 0; r < 3; ++r) {
        if (!rows[r].is_array() || rows[r].size() != 3) {
            SchemaError(location + ".rows[" + std::to_string(r) + "]", "expected three values");
        }
        for (std::size_t c = 0; c < 3; ++c) {
            result.rows[r * 3 + c] = RequireFinite(
                rows[r][c],
                location + ".rows[" + std::to_string(r) + "][" + std::to_string(c) + "]");
        }
    }
    return result;
}

Json HomographyJson(const Homography3x3& value) {
    return Json{{"rows", Json::array({
        Json::array({value.rows[0], value.rows[1], value.rows[2]}),
        Json::array({value.rows[3], value.rows[4], value.rows[5]}),
        Json::array({value.rows[6], value.rows[7], value.rows[8]}),
    })}};
}

Homography3x3 Multiply(const Homography3x3& a, const Homography3x3& b) {
    Homography3x3 result;
    result.rows.fill(0.0);
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 3; ++c) {
            for (std::size_t k = 0; k < 3; ++k) {
                result.rows[r * 3 + c] += a.rows[r * 3 + k] * b.rows[k * 3 + c];
            }
        }
    }
    return result;
}

void ValidateInversePair(const Homography3x3& forward, const Homography3x3& inverse, const std::string& location) {
    const auto product = Multiply(forward, inverse);
    const double scale = (product.rows[0] + product.rows[4] + product.rows[8]) / 3.0;
    if (!std::isfinite(scale) || std::abs(scale) < 1.0e-12) {
        throw std::runtime_error(location + ": singular homography pair");
    }
    const double tolerance = std::max(1.0, std::abs(scale)) * 1.0e-3;
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 3; ++c) {
            const double expected = r == c ? scale : 0.0;
            if (std::abs(product.rows[r * 3 + c] - expected) > tolerance) {
                throw std::runtime_error(location + ": forward and inverse homographies are inconsistent");
            }
        }
    }
}

RectificationProfile ParseProfile(const Json& object, const std::string& location) {
    RectificationProfile result;
    result.method = RequireString(object, "method", location);
    if (result.method != "uncalibrated" && result.method != "affine_vertical" && result.method != "affine_full") {
        SchemaError(location + ".method", "unsupported method");
    }

    if (const auto it = object.find("parameters"); it != object.end()) {
        if (!it->is_object()) SchemaError(location + ".parameters", "expected object");
        result.parameters = *it;
    }

    if (const auto it = object.find("quality"); it != object.end()) {
        if (!it->is_object()) SchemaError(location + ".quality", "expected object");
        result.quality.usedPairs = OptionalU32(*it, "used_pairs", 0, location + ".quality");
        result.quality.usedPoints = OptionalU32(*it, "used_points", 0, location + ".quality");
        result.quality.inlierPoints = OptionalU32(*it, "inlier_points", 0, location + ".quality");
        result.quality.meanAbsVerticalErrorPx = OptionalFinite(*it, "mean_abs_vertical_error_px", location + ".quality");
        result.quality.medianAbsVerticalErrorPx = OptionalFinite(*it, "median_abs_vertical_error_px", location + ".quality");
    }

    const auto& eyes = RequireObject(object, "eyes", location);
    const auto& left = RequireObject(eyes, "left", location + ".eyes");
    const auto& right = RequireObject(eyes, "right", location + ".eyes");
    result.leftForward = ParseHomography(RequireObject(left, "forward_pixel_homography", location + ".eyes.left"), location + ".eyes.left.forward_pixel_homography");
    result.leftInverse = ParseHomography(RequireObject(left, "inverse_pixel_homography", location + ".eyes.left"), location + ".eyes.left.inverse_pixel_homography");
    result.rightForward = ParseHomography(RequireObject(right, "forward_pixel_homography", location + ".eyes.right"), location + ".eyes.right.forward_pixel_homography");
    result.rightInverse = ParseHomography(RequireObject(right, "inverse_pixel_homography", location + ".eyes.right"), location + ".eyes.right.inverse_pixel_homography");
    ValidateInversePair(result.leftForward, result.leftInverse, location + ".eyes.left");
    ValidateInversePair(result.rightForward, result.rightInverse, location + ".eyes.right");
    return result;
}

Json ProfileJson(const RectificationProfile& profile) {
    Json quality{
        {"used_pairs", profile.quality.usedPairs},
        {"used_points", profile.quality.usedPoints},
        {"inlier_points", profile.quality.inlierPoints},
        {"mean_abs_vertical_error_px", profile.quality.meanAbsVerticalErrorPx ? Json(*profile.quality.meanAbsVerticalErrorPx) : Json(nullptr)},
        {"median_abs_vertical_error_px", profile.quality.medianAbsVerticalErrorPx ? Json(*profile.quality.medianAbsVerticalErrorPx) : Json(nullptr)},
    };
    return Json{
        {"method", profile.method},
        {"parameters", profile.parameters},
        {"quality", std::move(quality)},
        {"eyes", {
            {"left", {
                {"forward_pixel_homography", HomographyJson(profile.leftForward)},
                {"inverse_pixel_homography", HomographyJson(profile.leftInverse)},
            }},
            {"right", {
                {"forward_pixel_homography", HomographyJson(profile.rightForward)},
                {"inverse_pixel_homography", HomographyJson(profile.rightInverse)},
            }},
        }},
    };
}

void WriteTextAtomically(const std::filesystem::path& path, const std::string& text) {
    if (path.empty()) throw std::invalid_argument("JSON output path is empty");
    const auto parent = path.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);

    auto temporary = path;
    temporary += L".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) throw std::runtime_error("failed to open temporary JSON output: " + temporary.string());
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        stream.flush();
        if (!stream) throw std::runtime_error("failed to write temporary JSON output: " + temporary.string());
    }

#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD error = GetLastError();
        std::filesystem::remove(temporary);
        throw std::runtime_error("failed to replace JSON output file, Win32 error=" + std::to_string(error));
    }
#else
    std::error_code ec;
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("failed to replace JSON output file: " + ec.message());
    }
#endif
}

} // namespace

const RectificationProfile& CalibrationDocument::profile(const std::string& name) const {
    const auto it = profiles.find(name);
    if (it == profiles.end()) throw std::out_of_range("calibration profile does not exist: " + name);
    return it->second;
}

bool CalibrationDocument::hasProfile(const std::string& name) const noexcept {
    return profiles.find(name) != profiles.end();
}

nlohmann::json CalibrationDocument::toJson() const {
    ValidateCalibrationDocument(*this);
    Json profileObject = Json::object();
    for (const auto& [name, value] : profiles) profileObject[name] = ProfileJson(value);

    return Json{
        {"format", kFormat},
        {"version", kVersion},
        {"default_profile", defaultProfile},
        {"generator", {{"name", generatorName}, {"version", generatorVersion}}},
        {"image_geometry", {
            {"source_size", SizeJson(sourceSize)},
            {"calibration_input_size", SizeJson(calibrationInputSize)},
            {"rectified_output_size", SizeJson(rectifiedOutputSize)},
        }},
        {"coordinate_system", {
            {"origin", "top_left"},
            {"x_axis", "right"},
            {"y_axis", "down"},
            {"pixel_coordinate", "integer_is_pixel_center"},
            {"forward_mapping", "rectified_pixel = H * source_pixel"},
        }},
        {"preprocess", {
            {"resize", {
                {"mode", resizeMode},
                {"requested_width", nullptr},
                {"requested_height", nullptr},
                {"requested_scale", nullptr},
            }},
            {"right_order", rightOrder},
        }},
        {"checkerboard", {{"columns", boardColumns}, {"rows", boardRows}}},
        {"sampling", {
            {"filter", samplingFilter},
            {"border_mode", borderMode},
            {"border_rgba", borderRgba},
        }},
        {"profiles", std::move(profileObject)},
    };
}

CalibrationDocument CalibrationDocument::fromJson(const nlohmann::json& json) {
    if (!json.is_object()) SchemaError("root", "expected object");
    if (RequireString(json, "format", "root") != kFormat) SchemaError("root.format", "unsupported format");
    if (RequireInt(json, "version", "root") != kVersion) SchemaError("root.version", "unsupported version");

    CalibrationDocument result;
    result.defaultProfile = RequireString(json, "default_profile", "root");
    if (const auto it = json.find("generator"); it != json.end()) {
        if (!it->is_object()) SchemaError("root.generator", "expected object");
        result.generatorName = RequireString(*it, "name", "root.generator");
        result.generatorVersion = RequireInt(*it, "version", "root.generator");
    }

    const auto& geometry = RequireObject(json, "image_geometry", "root");
    result.sourceSize = ParseSize(RequireObject(geometry, "source_size", "root.image_geometry"), "root.image_geometry.source_size");
    result.calibrationInputSize = ParseSize(RequireObject(geometry, "calibration_input_size", "root.image_geometry"), "root.image_geometry.calibration_input_size");
    result.rectifiedOutputSize = ParseSize(RequireObject(geometry, "rectified_output_size", "root.image_geometry"), "root.image_geometry.rectified_output_size");

    const auto& coordinate = RequireObject(json, "coordinate_system", "root");
    if (RequireString(coordinate, "origin", "root.coordinate_system") != "top_left" ||
        RequireString(coordinate, "x_axis", "root.coordinate_system") != "right" ||
        RequireString(coordinate, "y_axis", "root.coordinate_system") != "down" ||
        RequireString(coordinate, "pixel_coordinate", "root.coordinate_system") != "integer_is_pixel_center" ||
        RequireString(coordinate, "forward_mapping", "root.coordinate_system") != "rectified_pixel = H * source_pixel") {
        SchemaError("root.coordinate_system", "unsupported coordinate convention");
    }

    if (const auto it = json.find("preprocess"); it != json.end()) {
        if (!it->is_object()) SchemaError("root.preprocess", "expected object");
        result.rightOrder = RequireString(*it, "right_order", "root.preprocess");
        const auto& resize = RequireObject(*it, "resize", "root.preprocess");
        result.resizeMode = RequireString(resize, "mode", "root.preprocess.resize");
    }

    if (const auto it = json.find("checkerboard"); it != json.end()) {
        if (!it->is_object()) SchemaError("root.checkerboard", "expected object");
        result.boardColumns = RequireU32(*it, "columns", "root.checkerboard");
        result.boardRows = RequireU32(*it, "rows", "root.checkerboard");
    }

    const auto& sampling = RequireObject(json, "sampling", "root");
    result.samplingFilter = RequireString(sampling, "filter", "root.sampling");
    result.borderMode = RequireString(sampling, "border_mode", "root.sampling");
    const auto& border = RequireArray(sampling, "border_rgba", "root.sampling");
    if (border.size() != 4) SchemaError("root.sampling.border_rgba", "expected four values");
    for (std::size_t i = 0; i < 4; ++i) {
        const double value = RequireFinite(border[i], "root.sampling.border_rgba[" + std::to_string(i) + "]");
        if (value < 0.0 || value > 1.0) SchemaError("root.sampling.border_rgba", "values must be in [0,1]");
        result.borderRgba[i] = static_cast<float>(value);
    }

    const auto& profileObject = RequireObject(json, "profiles", "root");
    for (auto it = profileObject.begin(); it != profileObject.end(); ++it) {
        if (!it.value().is_object()) SchemaError("root.profiles." + it.key(), "expected object");
        result.profiles.emplace(it.key(), ParseProfile(it.value(), "root.profiles." + it.key()));
    }
    ValidateCalibrationDocument(result);
    return result;
}

CalibrationDocument CalibrationDocument::loadJson(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("failed to open calibration JSON: " + path.string());
    Json json;
    try {
        stream >> json;
    } catch (const Json::exception& error) {
        throw std::runtime_error("failed to parse calibration JSON '" + path.string() + "': " + error.what());
    }
    return fromJson(json);
}

void CalibrationDocument::saveJsonAtomically(const std::filesystem::path& path, int indent) const {
    const std::string text = toJson().dump(indent) + "\n";
    WriteTextAtomically(path, text);
}

CalibrationDocument MakeIdentityCalibrationDocument(
    ImageSize sourceSize,
    ImageSize calibrationInputSize,
    ImageSize rectifiedOutputSize,
    std::uint32_t boardColumns,
    std::uint32_t boardRows,
    std::string rightOrder,
    std::string defaultProfile) {
    CalibrationDocument result;
    result.defaultProfile = std::move(defaultProfile);
    result.sourceSize = sourceSize;
    result.calibrationInputSize = calibrationInputSize;
    result.rectifiedOutputSize = rectifiedOutputSize;
    result.boardColumns = boardColumns;
    result.boardRows = boardRows;
    result.rightOrder = std::move(rightOrder);

    for (const std::string& method : {std::string("uncalibrated"), std::string("affine_vertical"), std::string("affine_full")}) {
        RectificationProfile profile;
        profile.method = method;
        result.profiles.emplace(method, std::move(profile));
    }
    ValidateCalibrationDocument(result);
    return result;
}

Homography3x3 InvertHomography(const Homography3x3& value) {
    const auto& m = value.rows;
    const double c00 = m[4] * m[8] - m[5] * m[7];
    const double c01 = -(m[3] * m[8] - m[5] * m[6]);
    const double c02 = m[3] * m[7] - m[4] * m[6];
    const double determinant = m[0] * c00 + m[1] * c01 + m[2] * c02;
    if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-12) {
        throw std::runtime_error("homography is singular");
    }
    const double inv = 1.0 / determinant;
    Homography3x3 result;
    result.rows = {
        c00 * inv,
        -(m[1] * m[8] - m[2] * m[7]) * inv,
        (m[1] * m[5] - m[2] * m[4]) * inv,
        c01 * inv,
        (m[0] * m[8] - m[2] * m[6]) * inv,
        -(m[0] * m[5] - m[2] * m[3]) * inv,
        c02 * inv,
        -(m[0] * m[7] - m[1] * m[6]) * inv,
        (m[0] * m[4] - m[1] * m[3]) * inv,
    };
    return result;
}

void ValidateCalibrationDocument(const CalibrationDocument& document) {
    if (!document.sourceSize.valid() || !document.calibrationInputSize.valid() || !document.rectifiedOutputSize.valid()) {
        throw std::invalid_argument("calibration document contains a zero image size");
    }
    if (document.defaultProfile.empty() || !document.hasProfile(document.defaultProfile)) {
        throw std::invalid_argument("calibration document default profile does not exist");
    }
    if (document.samplingFilter != "linear") throw std::invalid_argument("only linear sampling is supported");
    if (document.borderMode != "constant") throw std::invalid_argument("only constant border mode is supported");
    if (document.rightOrder != "same" && document.rightOrder != "flip_x" &&
        document.rightOrder != "flip_y" && document.rightOrder != "rot180") {
        throw std::invalid_argument("unsupported right-order value");
    }
    for (float value : document.borderRgba) {
        if (!std::isfinite(value) || value < 0.0f || value > 1.0f) {
            throw std::invalid_argument("border RGBA values must be finite and normalized");
        }
    }
    for (const auto& [name, profile] : document.profiles) {
        if (name.empty()) throw std::invalid_argument("calibration profile name is empty");
        if (profile.method != "uncalibrated" && profile.method != "affine_vertical" && profile.method != "affine_full") {
            throw std::invalid_argument("unsupported calibration method");
        }
        ValidateInversePair(profile.leftForward, profile.leftInverse, "profile " + name + " left");
        ValidateInversePair(profile.rightForward, profile.rightInverse, "profile " + name + " right");
    }
}

} // namespace Vdca::StereoCalibration
