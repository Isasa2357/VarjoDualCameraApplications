#include "AppOptions.hpp"

#include <algorithm>
#include <cwctype>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string_view>

namespace Vdca::RealtimeCalibrationApp {
namespace {

std::wstring RequireValue(int& index, int argc, wchar_t** argv) {
    if (++index >= argc || !argv[index] || argv[index][0] == L'\0') {
        throw std::invalid_argument("missing command-line option value");
    }
    return argv[index];
}

std::string NarrowAscii(const std::wstring& value) {
    std::string result;
    result.reserve(value.size());
    for (wchar_t c : value) {
        if (c < 0 || c > 127) throw std::invalid_argument("option value must be ASCII");
        result.push_back(static_cast<char>(c));
    }
    return result;
}

std::wstring Upper(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towupper(c));
    });
    return value;
}

std::uint32_t U32(const std::wstring& value) {
    std::size_t used = 0;
    const auto parsed = std::stoull(value, &used, 10);
    if (used != value.size() || parsed > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("invalid uint32 option value");
    }
    return static_cast<std::uint32_t>(parsed);
}

std::size_t Size(const std::wstring& value) {
    std::size_t used = 0;
    const auto parsed = std::stoull(value, &used, 10);
    if (used != value.size() || parsed > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument("invalid size option value");
    }
    return static_cast<std::size_t>(parsed);
}

std::int64_t I64(const std::wstring& value) {
    std::size_t used = 0;
    const auto parsed = std::stoll(value, &used, 10);
    if (used != value.size()) throw std::invalid_argument("invalid signed integer option value");
    return parsed;
}

int Int(const std::wstring& value) {
    const auto parsed = I64(value);
    if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
        throw std::invalid_argument("integer option value is outside int range");
    }
    return static_cast<int>(parsed);
}

double Double(const std::wstring& value) {
    std::size_t used = 0;
    const double parsed = std::stod(value, &used);
    if (used != value.size()) throw std::invalid_argument("invalid floating-point option value");
    return parsed;
}

float Float(const std::wstring& value) {
    return static_cast<float>(Double(value));
}

void Validate(const AppOptions& o) {
    if (o.leftCameraIndex < 0 || o.rightCameraIndex < 0 || o.leftCameraIndex == o.rightCameraIndex) {
        throw std::invalid_argument("left/right camera indices must be distinct non-negative values");
    }
    if (o.width == 0 || o.height == 0 || o.fpsNumerator == 0 || o.fpsDenominator == 0) {
        throw std::invalid_argument("camera size and frame rate must be non-zero");
    }
    if ((o.processingWidth == 0) != (o.processingHeight == 0) ||
        (o.outputWidth == 0) != (o.outputHeight == 0)) {
        throw std::invalid_argument("processing/output width and height must be specified together");
    }
    if (o.boardColumns < 2 || o.boardRows < 2) throw std::invalid_argument("checkerboard dimensions must be at least 2x2");
    if (o.maxObservations == 0 || o.minObservations == 0 || o.minObservations > o.maxObservations) {
        throw std::invalid_argument("invalid observation-count configuration");
    }
    if (o.minCornerMotionPx < 0.0 || o.ransacThresholdPx <= 0.0) {
        throw std::invalid_argument("calibration thresholds are invalid");
    }
    if (!o.rightOrder.empty() && o.rightOrder != "same" && o.rightOrder != "flip_x" && o.rightOrder != "flip_y" && o.rightOrder != "rot180") {
        throw std::invalid_argument("--right-order must be same, flip_x, flip_y, or rot180");
    }
    if (!o.activeProfile.empty() && o.activeProfile != "uncalibrated" &&
        o.activeProfile != "affine_vertical" && o.activeProfile != "affine_full") {
        throw std::invalid_argument("--profile must be uncalibrated, affine_vertical, or affine_full");
    }
    if (o.displayCaptureQueueCapacity == 0 || o.calibrationCaptureQueueCapacity == 0 ||
        o.displaySyncOutputCapacity == 0 || o.calibrationSyncOutputCapacity == 0 ||
        o.syncCandidateCapacity == 0) {
        throw std::invalid_argument("queue capacities must be non-zero");
    }
    if (o.syncToleranceMicroseconds < 0) throw std::invalid_argument("sync tolerance must be non-negative");
    if (!(o.planeWidthMeters > 0.0f) || !(o.planeDistanceMeters > 0.0f)) {
        throw std::invalid_argument("plane width and distance must be positive");
    }
    if (o.startupTimeoutMilliseconds == 0) throw std::invalid_argument("startup timeout must be non-zero");
}

} // namespace

AppOptions ParseAppOptions(int argc, wchar_t** argv) {
    AppOptions o;
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg = argv[i] ? argv[i] : L"";
        if (arg == L"--help" || arg == L"-h") o.showHelp = true;
        else if (arg == L"--left") o.leftCameraIndex = Int(RequireValue(i, argc, argv));
        else if (arg == L"--right") o.rightCameraIndex = Int(RequireValue(i, argc, argv));
        else if (arg == L"--width") o.width = U32(RequireValue(i, argc, argv));
        else if (arg == L"--height") o.height = U32(RequireValue(i, argc, argv));
        else if (arg == L"--processing-width") o.processingWidth = U32(RequireValue(i, argc, argv));
        else if (arg == L"--processing-height") o.processingHeight = U32(RequireValue(i, argc, argv));
        else if (arg == L"--output-width") o.outputWidth = U32(RequireValue(i, argc, argv));
        else if (arg == L"--output-height") o.outputHeight = U32(RequireValue(i, argc, argv));
        else if (arg == L"--fps-num") o.fpsNumerator = U32(RequireValue(i, argc, argv));
        else if (arg == L"--fps-den") o.fpsDenominator = U32(RequireValue(i, argc, argv));
        else if (arg == L"--subtype") o.subtype = Upper(RequireValue(i, argc, argv));
        else if (arg == L"--shader-dir") o.shaderDirectory = RequireValue(i, argc, argv);
        else if (arg == L"--board-cols") o.boardColumns = U32(RequireValue(i, argc, argv));
        else if (arg == L"--board-rows") o.boardRows = U32(RequireValue(i, argc, argv));
        else if (arg == L"--right-order") o.rightOrder = NarrowAscii(RequireValue(i, argc, argv));
        else if (arg == L"--profile") o.activeProfile = NarrowAscii(RequireValue(i, argc, argv));
        else if (arg == L"--max-observations") o.maxObservations = Size(RequireValue(i, argc, argv));
        else if (arg == L"--min-observations") o.minObservations = Size(RequireValue(i, argc, argv));
        else if (arg == L"--min-corner-motion") o.minCornerMotionPx = Double(RequireValue(i, argc, argv));
        else if (arg == L"--ransac-threshold") o.ransacThresholdPx = Double(RequireValue(i, argc, argv));
        else if (arg == L"--no-fit-canvas") o.fitCanvas = false;
        else if (arg == L"--no-sb") o.useChessboardSb = false;
        else if (arg == L"--initial-json") o.initialJson = RequireValue(i, argc, argv);
        else if (arg == L"--output-json") o.outputJson = RequireValue(i, argc, argv);
        else if (arg == L"--display-capture-queue") o.displayCaptureQueueCapacity = Size(RequireValue(i, argc, argv));
        else if (arg == L"--calibration-capture-queue") o.calibrationCaptureQueueCapacity = Size(RequireValue(i, argc, argv));
        else if (arg == L"--display-sync-queue") o.displaySyncOutputCapacity = Size(RequireValue(i, argc, argv));
        else if (arg == L"--calibration-sync-queue") o.calibrationSyncOutputCapacity = Size(RequireValue(i, argc, argv));
        else if (arg == L"--sync-candidates") o.syncCandidateCapacity = Size(RequireValue(i, argc, argv));
        else if (arg == L"--sync-tolerance-us") o.syncToleranceMicroseconds = I64(RequireValue(i, argc, argv));
        else if (arg == L"--plane-width") o.planeWidthMeters = Float(RequireValue(i, argc, argv));
        else if (arg == L"--plane-distance") o.planeDistanceMeters = Float(RequireValue(i, argc, argv));
        else if (arg == L"--plane-y") o.planeVerticalOffsetMeters = Float(RequireValue(i, argc, argv));
        else if (arg == L"--placement") {
            const auto value = Upper(RequireValue(i, argc, argv));
            if (value == L"HEAD" || value == L"HEADRELATIVE") o.placement = PlanePlacement::HeadRelative;
            else if (value == L"WORLD") o.placement = PlanePlacement::World;
            else throw std::invalid_argument("--placement must be head or world");
        }
        else if (arg == L"--startup-timeout-ms") o.startupTimeoutMilliseconds = U32(RequireValue(i, argc, argv));
        else if (arg == L"--log-every") o.logEveryFrames = U32(RequireValue(i, argc, argv));
        else if (arg == L"--no-d3d-debug") o.enableD3dDebugLayer = false;
        else throw std::invalid_argument("unknown command-line option");
    }
    if (!o.showHelp) Validate(o);
    return o;
}

void PrintUsage(std::wostream& out) {
    out << L"RealtimeStereoCalibration\n\n"
        << L"Camera: --left N --right N --width W --height H --fps-num N --fps-den N --subtype TYPE\n"
        << L"Processing: [--processing-width W --processing-height H] [--output-width W --output-height H]\n"
        << L"Checkerboard: --board-cols N --board-rows N [--right-order same|flip_x|flip_y|rot180]\n"
        << L"Calibration: [--profile uncalibrated|affine_vertical|affine_full]\n"
        << L"             [--initial-json PATH] [--output-json PATH]\n"
        << L"             [--max-observations N] [--min-observations N]\n"
        << L"             [--min-corner-motion PX] [--ransac-threshold PX] [--no-fit-canvas] [--no-sb]\n"
        << L"Plane: --plane-width M --plane-distance M --plane-y M --placement head|world\n"
        << L"Queues: --display-capture-queue N --calibration-capture-queue N\n"
        << L"        --display-sync-queue N --calibration-sync-queue N --sync-candidates N\n"
        << L"Other: --shader-dir PATH --sync-tolerance-us N --startup-timeout-ms N --log-every N\n"
        << L"       --no-d3d-debug --help\n\n"
        << L"Press Esc or Ctrl+C to exit.\n";
}

} // namespace Vdca::RealtimeCalibrationApp
