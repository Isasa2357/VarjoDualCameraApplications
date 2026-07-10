#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>

namespace Vdca::RealtimeCalibrationApp {

enum class PlanePlacement { HeadRelative, World };

struct AppOptions {
    int leftCameraIndex = 0;
    int rightCameraIndex = 1;
    std::uint32_t width = 1920;
    std::uint32_t height = 1080;
    std::uint32_t processingWidth = 0;
    std::uint32_t processingHeight = 0;
    std::uint32_t outputWidth = 0;
    std::uint32_t outputHeight = 0;
    std::uint32_t fpsNumerator = 60;
    std::uint32_t fpsDenominator = 1;
    std::wstring subtype = L"NV12";
    std::filesystem::path shaderDirectory;

    std::uint32_t boardColumns = 12;
    std::uint32_t boardRows = 9;
    std::string rightOrder;
    std::string activeProfile;
    std::size_t maxObservations = 30;
    std::size_t minObservations = 1;
    double minCornerMotionPx = 2.0;
    double ransacThresholdPx = 1.5;
    bool fitCanvas = true;
    bool useChessboardSb = false;

    std::filesystem::path initialJson;
    std::filesystem::path outputJson;

    std::size_t displayCaptureQueueCapacity = 3;
    std::size_t calibrationCaptureQueueCapacity = 1;
    std::size_t displaySyncOutputCapacity = 2;
    std::size_t calibrationSyncOutputCapacity = 1;
    std::size_t syncCandidateCapacity = 16;
    std::int64_t syncToleranceMicroseconds = 5000;

    float planeWidthMeters = 1.0f;
    float planeDistanceMeters = 1.0f;
    float planeVerticalOffsetMeters = 0.0f;
    PlanePlacement placement = PlanePlacement::HeadRelative;

    std::uint32_t startupTimeoutMilliseconds = 10000;
    std::uint32_t logEveryFrames = 120;
    bool enableD3dDebugLayer = true;
    bool showHelp = false;
};

AppOptions ParseAppOptions(int argc, wchar_t** argv);
void PrintUsage(std::wostream& out);

} // namespace Vdca::RealtimeCalibrationApp
