#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>

namespace Vdca {

enum class PlanePlacement {
    HeadRelative,
    World,
};

struct AppOptions {
    int leftCameraIndex = 0;
    int rightCameraIndex = 1;

    std::uint32_t width = 1920;
    std::uint32_t height = 1080;
    std::uint32_t fpsNumerator = 60;
    std::uint32_t fpsDenominator = 1;
    std::wstring subtype = L"NV12";

    std::filesystem::path shaderDirectory;

    std::int64_t syncToleranceMicroseconds = 5000;
    std::size_t captureQueueCapacity = 4;
    std::size_t syncCandidateCapacity = 16;
    std::size_t syncOutputQueueCapacity = 2;

    float planeWidthMeters = 1.0f;
    float planeDistanceMeters = 1.0f;
    float planeVerticalOffsetMeters = 0.0f;
    PlanePlacement placement = PlanePlacement::HeadRelative;

    std::uint32_t startupTimeoutMilliseconds = 10000;
    std::uint32_t logEveryPairs = 120;

    bool enableD3dDebugLayer = true;
    bool showHelp = false;
};

AppOptions ParseAppOptions(int argc, wchar_t** argv);
void PrintUsage(std::wostream& out);

} // namespace Vdca
