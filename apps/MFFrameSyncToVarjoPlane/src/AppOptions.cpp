#include "AppOptions.hpp"

#include <algorithm>
#include <cwctype>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string_view>

namespace Vdca {
namespace {

std::wstring RequireValue(int& index, int argc, wchar_t** argv, std::wstring_view option) {
    if (index + 1 >= argc) {
        throw std::invalid_argument("missing value for command-line option");
    }
    ++index;
    if (!argv[index] || argv[index][0] == L'\0') {
        throw std::invalid_argument("empty value for command-line option");
    }
    (void)option;
    return argv[index];
}

std::uint32_t ParseU32(const std::wstring& text, std::wstring_view option) {
    std::size_t consumed = 0;
    const unsigned long value = std::stoul(text, &consumed, 10);
    if (consumed != text.size() || value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("invalid unsigned integer command-line value");
    }
    (void)option;
    return static_cast<std::uint32_t>(value);
}

std::size_t ParseSize(const std::wstring& text, std::wstring_view option) {
    std::size_t consumed = 0;
    const unsigned long long value = std::stoull(text, &consumed, 10);
    if (consumed != text.size() || value > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument("invalid size command-line value");
    }
    (void)option;
    return static_cast<std::size_t>(value);
}

std::int64_t ParseI64(const std::wstring& text, std::wstring_view option) {
    std::size_t consumed = 0;
    const long long value = std::stoll(text, &consumed, 10);
    if (consumed != text.size()) {
        throw std::invalid_argument("invalid signed integer command-line value");
    }
    (void)option;
    return static_cast<std::int64_t>(value);
}

int ParseInt(const std::wstring& text, std::wstring_view option) {
    const auto value = ParseI64(text, option);
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) {
        throw std::invalid_argument("integer command-line value is outside int range");
    }
    return static_cast<int>(value);
}

float ParseFloat(const std::wstring& text, std::wstring_view option) {
    std::size_t consumed = 0;
    const float value = std::stof(text, &consumed);
    if (consumed != text.size()) {
        throw std::invalid_argument("invalid floating-point command-line value");
    }
    (void)option;
    return value;
}

std::string ParseAscii(const std::wstring& text, std::wstring_view option) {
    std::string result;
    result.reserve(text.size());
    for (const wchar_t c : text) {
        if (c < 0 || c > 0x7f) {
            throw std::invalid_argument("command-line value must contain ASCII characters only");
        }
        result.push_back(static_cast<char>(c));
    }
    (void)option;
    return result;
}

std::wstring Uppercase(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towupper(c));
    });
    return text;
}

bool IsSupportedRectificationProfile(const std::string& profile) noexcept {
    return profile == "uncalibrated" ||
           profile == "affine_vertical" ||
           profile == "affine_full";
}

void Validate(const AppOptions& options) {
    if (options.leftCameraIndex < 0 || options.rightCameraIndex < 0) {
        throw std::invalid_argument("camera indices must be non-negative");
    }
    if (options.leftCameraIndex == options.rightCameraIndex) {
        throw std::invalid_argument("left and right camera indices must be different");
    }
    if (options.width == 0 || options.height == 0) {
        throw std::invalid_argument("camera width and height must be greater than zero");
    }
    if (options.fpsNumerator == 0 || options.fpsDenominator == 0) {
        throw std::invalid_argument("frame-rate numerator and denominator must be greater than zero");
    }
    if (options.captureQueueCapacity == 0 ||
        options.syncCandidateCapacity == 0 ||
        options.syncOutputQueueCapacity == 0) {
        throw std::invalid_argument("queue capacities must be greater than zero");
    }
    if (options.syncToleranceMicroseconds < 0 ||
        options.syncToleranceMicroseconds > (std::numeric_limits<std::int64_t>::max() / 10)) {
        throw std::invalid_argument("sync tolerance is outside the supported range");
    }
    if (!(options.planeWidthMeters > 0.0f) || !(options.planeDistanceMeters > 0.0f)) {
        throw std::invalid_argument("plane width and distance must be greater than zero");
    }
    if (options.startupTimeoutMilliseconds == 0) {
        throw std::invalid_argument("startup timeout must be greater than zero");
    }
    if (!options.rectificationProfile.empty() && options.rectificationPath.empty()) {
        throw std::invalid_argument("--rectification-profile requires --rectification");
    }
    if (!options.rectificationProfile.empty() &&
        !IsSupportedRectificationProfile(options.rectificationProfile)) {
        throw std::invalid_argument(
            "--rectification-profile must be uncalibrated, affine_vertical, or affine_full");
    }
}

} // namespace

AppOptions ParseAppOptions(int argc, wchar_t** argv) {
    AppOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg = argv[i] ? argv[i] : L"";

        if (arg == L"--help" || arg == L"-h") {
            options.showHelp = true;
        } else if (arg == L"--left") {
            options.leftCameraIndex = ParseInt(RequireValue(i, argc, argv, arg), arg);
        } else if (arg == L"--right") {
            options.rightCameraIndex = ParseInt(RequireValue(i, argc, argv, arg), arg);
        } else if (arg == L"--width") {
            options.width = ParseU32(RequireValue(i, argc, argv, arg), arg);
        } else if (arg == L"--height") {
            options.height = ParseU32(RequireValue(i, argc, argv, arg), arg);
        } else if (arg == L"--fps-num") {
            options.fpsNumerator = ParseU32(RequireValue(i, argc, argv, arg), arg);
        } else if (arg == L"--fps-den") {
            options.fpsDenominator = ParseU32(RequireValue(i, argc, argv, arg), arg);
        } else if (arg == L"--subtype") {
            options.subtype = Uppercase(RequireValue(i, argc, argv, arg));
        } else if (arg == L"--shader-dir") {
            options.shaderDirectory = RequireValue(i, argc, argv, arg);
        } else if (arg == L"--rectification") {
            options.rectificationPath = RequireValue(i, argc, argv, arg);
        } else if (arg == L"--rectification-profile") {
            options.rectificationProfile = ParseAscii(RequireValue(i, argc, argv, arg), arg);
        } else if (arg == L"--sync-tolerance-us") {
            options.syncToleranceMicroseconds = ParseI64(RequireValue(i, argc, argv, arg), arg);
        } else if (arg == L"--capture-queue") {
            options.captureQueueCapacity = ParseSize(RequireValue(i, argc, argv, arg), arg);
        } else if (arg == L"--sync-candidates") {
            options.syncCandidateCapacity = ParseSize(RequireValue(i, argc, argv, arg), arg);
        } else if (arg == L"--sync-output-queue") {
            options.syncOutputQueueCapacity = ParseSize(RequireValue(i, argc, argv, arg), arg);
        } else if (arg == L"--plane-width") {
            options.planeWidthMeters = ParseFloat(RequireValue(i, argc, argv, arg), arg);
        } else if (arg == L"--plane-distance") {
            options.planeDistanceMeters = ParseFloat(RequireValue(i, argc, argv, arg), arg);
        } else if (arg == L"--plane-y") {
            options.planeVerticalOffsetMeters = ParseFloat(RequireValue(i, argc, argv, arg), arg);
        } else if (arg == L"--placement") {
            const auto value = Uppercase(RequireValue(i, argc, argv, arg));
            if (value == L"HEAD" || value == L"HEADRELATIVE") {
                options.placement = PlanePlacement::HeadRelative;
            } else if (value == L"WORLD") {
                options.placement = PlanePlacement::World;
            } else {
                throw std::invalid_argument("--placement must be head or world");
            }
        } else if (arg == L"--startup-timeout-ms") {
            options.startupTimeoutMilliseconds = ParseU32(RequireValue(i, argc, argv, arg), arg);
        } else if (arg == L"--log-every") {
            options.logEveryPairs = ParseU32(RequireValue(i, argc, argv, arg), arg);
        } else if (arg == L"--no-d3d-debug") {
            options.enableD3dDebugLayer = false;
        } else {
            throw std::invalid_argument("unknown command-line option");
        }
    }

    if (!options.showHelp) {
        Validate(options);
    }
    return options;
}

void PrintUsage(std::wostream& out) {
    out << L"MFFrameSyncToVarjoPlane\n\n"
        << L"Usage:\n"
        << L"  MFFrameSyncToVarjoPlane.exe [options]\n\n"
        << L"Camera options (MFFrameSource exact native-format match):\n"
        << L"  --left INDEX                 Left camera index (default: 0)\n"
        << L"  --right INDEX                Right camera index (default: 1)\n"
        << L"  --width PIXELS               Native camera width (default: 1920)\n"
        << L"  --height PIXELS              Native camera height (default: 1080)\n"
        << L"  --fps-num N                  Frame-rate numerator (default: 60)\n"
        << L"  --fps-den N                  Frame-rate denominator (default: 1)\n"
        << L"  --subtype TYPE               NV12 | P010 | RGB32 | ARGB32\n"
        << L"  --shader-dir PATH            D3D12Processing shader directory\n\n"
        << L"Stereo rectification options:\n"
        << L"  --rectification PATH         vdca.stereo_rectification version 1 JSON\n"
        << L"  --rectification-profile NAME uncalibrated | affine_vertical | affine_full\n"
        << L"                               Omit NAME to use JSON default_profile\n\n"
        << L"Synchronization options:\n"
        << L"  --sync-tolerance-us N        Maximum adjusted timestamp difference (default: 5000)\n"
        << L"  --capture-queue N            Per-camera queue capacity (default: 4)\n"
        << L"  --sync-candidates N          Candidate capacity per eye (default: 16)\n"
        << L"  --sync-output-queue N        Synchronized-pair queue capacity (default: 2)\n\n"
        << L"Plane options:\n"
        << L"  --plane-width METERS         Plane width (default: 1.0)\n"
        << L"  --plane-distance METERS      Distance in front of origin/head (default: 1.0)\n"
        << L"  --plane-y METERS             Vertical offset (default: 0.0)\n"
        << L"  --placement head|world       Placement mode (default: head)\n\n"
        << L"Diagnostics:\n"
        << L"  --startup-timeout-ms N       First-pair timeout (default: 10000)\n"
        << L"  --log-every N                Print sync statistics every N pairs; 0 disables\n"
        << L"  --no-d3d-debug               Disable the D3D12 debug layer\n"
        << L"  --help, -h                   Show this help\n\n"
        << L"Press Esc or Ctrl+C to exit.\n";
}

} // namespace Vdca
