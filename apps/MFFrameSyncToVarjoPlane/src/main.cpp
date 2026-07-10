#include "AppOptions.hpp"
#include "StereoPlaneSurface.hpp"

#include <MFFrameSource/MFD3D12CameraCaptureThread.hpp>
#include <MFFrameSource/MFD3D12CameraSyncThread.hpp>
#include <MFFrameSource/MFPlatformContext.hpp>

#include <D3D12Helper/D3D12Core/D3D12Core.hpp>

#include <VarjoToolkit/Core/VarjoSession.hpp>
#include <VarjoXR/VarjoXR.hpp>

#include <Windows.h>
#include <mfapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

std::atomic_bool gStopRequested{false};

BOOL WINAPI ConsoleControlHandler(DWORD controlType) {
    switch (controlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        gStopRequested.store(true, std::memory_order_relaxed);
        return TRUE;
    default:
        return FALSE;
    }
}

void LogInfo(const std::wstring& message) {
    std::wcout << L"[INFO] " << message << L'\n';
}

void LogError(const std::wstring& message) {
    std::wcerr << L"[ERROR] " << message << L'\n';
}

std::filesystem::path ExecutableDirectory() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        throw std::runtime_error("GetModuleFileNameW failed");
    }
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path ResolveShaderDirectory(const Vdca::AppOptions& options) {
    if (!options.shaderDirectory.empty()) {
        if (!std::filesystem::is_directory(options.shaderDirectory)) {
            throw std::runtime_error("--shader-dir does not point to a directory");
        }
        return std::filesystem::absolute(options.shaderDirectory);
    }

    const auto bundled = ExecutableDirectory() / L"shaders" / L"D3D12Processing";
    if (std::filesystem::is_directory(bundled)) {
        return bundled;
    }

    // An empty path lets D3D12Processing use its own configured search path.
    return {};
}

GUID ParseSubtype(const std::wstring& subtype) {
    if (subtype == L"NV12") return MFVideoFormat_NV12;
    if (subtype == L"P010") return MFVideoFormat_P010;
    if (subtype == L"RGB32") return MFVideoFormat_RGB32;
    if (subtype == L"ARGB32") return MFVideoFormat_ARGB32;
    throw std::invalid_argument("unsupported camera subtype");
}

MFFrameSource::MFD3D12CameraCaptureThreadConfig MakeCaptureConfig(
    int cameraIndex,
    const Vdca::AppOptions& options,
    const std::filesystem::path& shaderDirectory) {
    MFFrameSource::MFD3D12CameraCaptureThreadConfig config;
    config.selector.deviceIndex = cameraIndex;
    config.capture.input.width = options.width;
    config.capture.input.height = options.height;
    config.capture.input.fps.numerator = options.fpsNumerator;
    config.capture.input.fps.denominator = options.fpsDenominator;
    config.capture.input.subtype = ParseSubtype(options.subtype);
    config.capture.outputWidth = options.width;
    config.capture.outputHeight = options.height;
    config.capture.outputFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    config.capture.processingShaderDirectory = shaderDirectory.wstring();
    config.capture.framePoolSize = 4;
    config.capture.waitForGpuCompletionOnRead = false;
    config.defaultQueueCapacity = options.captureQueueCapacity;
    config.clonePoolSize = std::max<std::size_t>(8, options.captureQueueCapacity * 3);
    return config;
}

void ThrowCameraOpenError(
    const wchar_t* eye,
    const MFFrameSource::MFD3D12CameraCaptureThread& capture) {
    const auto& error = capture.lastError();
    std::wstring message = eye;
    message += L" camera open failed";
    if (!error.where.empty()) {
        message += L" at ";
        message += error.where;
    }
    if (!error.message.empty()) {
        message += L": ";
        message += error.message;
    }
    LogError(message);
    throw std::runtime_error("camera open failed; see the preceding detailed log");
}

void LogSelectedFormat(
    const wchar_t* eye,
    const MFFrameSource::MFCameraFormatInfo& format) {
    std::wcout << L"[INFO] " << eye << L" camera selected format: "
               << format.width << L"x" << format.height << L" @ "
               << format.fps.numerator << L"/" << format.fps.denominator
               << L", DXGI=" << MFFrameSource::DxgiFormatName(format.dxgiFormat)
               << L'\n';
}

std::shared_ptr<VarjoSession> CreateVarjoSession() {
    auto session = std::make_shared<VarjoSession>();
    if (!session->valid() && !session->initialize()) {
        throw std::runtime_error("failed to initialize Varjo session: " + session->lastError());
    }
    return session;
}

class CaptureStopGuard {
public:
    CaptureStopGuard(
        MFFrameSource::MFD3D12CameraCaptureThread& left,
        MFFrameSource::MFD3D12CameraCaptureThread& right,
        MFFrameSource::MFD3D12CameraSyncThread& sync)
        : left_(left), right_(right), sync_(sync) {}

    ~CaptureStopGuard() { stop(); }

    void stop() noexcept {
        if (stopped_) return;
        stopped_ = true;
        try { sync_.stop(); } catch (...) {}
        try { left_.stop(); } catch (...) {}
        try { right_.stop(); } catch (...) {}
    }

private:
    MFFrameSource::MFD3D12CameraCaptureThread& left_;
    MFFrameSource::MFD3D12CameraCaptureThread& right_;
    MFFrameSource::MFD3D12CameraSyncThread& sync_;
    bool stopped_ = false;
};

void RethrowWorkerExceptions(
    const MFFrameSource::MFD3D12CameraCaptureThread& left,
    const MFFrameSource::MFD3D12CameraCaptureThread& right,
    const MFFrameSource::MFD3D12CameraSyncThread& sync) {
    left.rethrowWorkerExceptionIfAny();
    right.rethrowWorkerExceptionIfAny();
    sync.rethrowWorkerExceptionIfAny();
}

void PrintPeriodicStats(
    const MFFrameSource::MFD3D12CameraCaptureThread& left,
    const MFFrameSource::MFD3D12CameraCaptureThread& right,
    const MFFrameSource::MFD3D12CameraSyncThread& sync,
    const Vdca::StereoPlaneSurface& surface) {
    const auto leftStats = left.stats();
    const auto rightStats = right.stats();
    const auto syncStats = sync.stats();

    std::wcout << L"[SYNC] pair=" << surface.lastPairNumber()
               << L" adjustedDiff=" << std::fixed << std::setprecision(3)
               << (static_cast<double>(surface.lastAdjustedDiff100ns()) / 10.0)
               << L" us"
               << L" | in(L/R)=" << syncStats.leftFramesIn << L"/" << syncStats.rightFramesIn
               << L" out=" << syncStats.pairsOut
               << L" drop(L/R)=" << syncStats.droppedLeft << L"/" << syncStats.droppedRight
               << L" captureDelivered(L/R)=" << leftStats.framesDelivered << L"/" << rightStats.framesDelivered
               << L'\n';
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        const Vdca::AppOptions options = Vdca::ParseAppOptions(argc, argv);
        if (options.showHelp) {
            Vdca::PrintUsage(std::wcout);
            return 0;
        }

        SetConsoleCtrlHandler(ConsoleControlHandler, TRUE);
        LogInfo(L"initializing Media Foundation, D3D12, and VarjoXR");

        MFFrameSource::MFPlatformContext mediaFoundation;
        if (!mediaFoundation.initialized()) {
            throw std::runtime_error("Media Foundation platform initialization failed");
        }

        D3D12CoreLib::D3D12CoreConfig d3dConfig{};
        d3dConfig.enableDebugLayer = options.enableD3dDebugLayer;
        d3dConfig.enableInfoQueue = options.enableD3dDebugLayer;
        d3dConfig.enableDred = true;
        d3dConfig.preferHighPerformanceAdapter = true;
        d3dConfig.allowWarpAdapter = false;
        d3dConfig.createDirectQueue = true;
        d3dConfig.createCopyQueue = true;
        auto core = D3D12CoreLib::D3D12Core::CreateShared(d3dConfig);

        const auto shaderDirectory = ResolveShaderDirectory(options);
        if (shaderDirectory.empty()) {
            LogInfo(L"D3D12Processing shader directory: helper default search");
        } else {
            LogInfo(L"D3D12Processing shader directory: " + shaderDirectory.wstring());
        }

        auto session = CreateVarjoSession();
        VarjoXR::Backends::D3D12::D3D12BackendDesc backendDesc{};
        backendDesc.frameResourceCount = 3;
        auto backend = VarjoXR::Backends::D3D12::CreateBackend(core, backendDesc);
        VarjoXR::XRSpace space({session, std::move(backend)});
        auto& d3d12Backend = static_cast<VarjoXR::Backends::D3D12::D3D12Backend&>(space.backend());

        MFFrameSource::MFD3D12CameraCaptureThread leftCapture;
        MFFrameSource::MFD3D12CameraCaptureThread rightCapture;
        MFFrameSource::MFD3D12CameraSyncThread sync;
        CaptureStopGuard stopGuard(leftCapture, rightCapture, sync);

        const auto leftConfig = MakeCaptureConfig(options.leftCameraIndex, options, shaderDirectory);
        const auto rightConfig = MakeCaptureConfig(options.rightCameraIndex, options, shaderDirectory);

        LogInfo(L"opening left camera index " + std::to_wstring(options.leftCameraIndex));
        if (!leftCapture.open(leftConfig, core)) {
            ThrowCameraOpenError(L"left", leftCapture);
        }
        LogSelectedFormat(L"left", leftCapture.selectedFormat());

        LogInfo(L"opening right camera index " + std::to_wstring(options.rightCameraIndex));
        if (!rightCapture.open(rightConfig, core)) {
            ThrowCameraOpenError(L"right", rightCapture);
        }
        LogSelectedFormat(L"right", rightCapture.selectedFormat());

        auto leftQueue = leftCapture.createQueue(options.captureQueueCapacity);
        auto rightQueue = rightCapture.createQueue(options.captureQueueCapacity);

        MFFrameSource::MFD3D12CameraSyncThreadConfig syncConfig;
        syncConfig.maxAdjustedDiff100ns = options.syncToleranceMicroseconds * 10;
        syncConfig.estimateBaseline = true;
        syncConfig.candidateCapacity = options.syncCandidateCapacity;
        syncConfig.outputQueueCapacity = options.syncOutputQueueCapacity;
        syncConfig.outputOverflowPolicy = ThreadKit::Queues::QueueOverflowPolicy::DropOldest;

        if (!sync.open(leftQueue, rightQueue, syncConfig)) {
            throw std::runtime_error("MFD3D12CameraSyncThread::open failed");
        }

        leftCapture.start();
        rightCapture.start();
        sync.start();
        LogInfo(L"capture and synchronization threads started");

        auto synchronizedQueue = sync.outputQueue();
        auto firstFrame = synchronizedQueue->waitPopLatestFor(
            std::chrono::milliseconds(options.startupTimeoutMilliseconds));
        if (!firstFrame) {
            RethrowWorkerExceptions(leftCapture, rightCapture, sync);
            throw std::runtime_error("timed out waiting for the first synchronized stereo frame");
        }

        if (firstFrame->left.width() != firstFrame->right.width() ||
            firstFrame->left.height() != firstFrame->right.height() ||
            firstFrame->left.format() != firstFrame->right.format()) {
            throw std::runtime_error("left/right synchronized frame formats do not match");
        }

        Vdca::StereoPlaneSurfaceDesc surfaceDesc;
        surfaceDesc.width = firstFrame->left.width();
        surfaceDesc.height = firstFrame->left.height();
        surfaceDesc.format = firstFrame->left.format();
        surfaceDesc.planeWidthMeters = options.planeWidthMeters;
        surfaceDesc.planeDistanceMeters = options.planeDistanceMeters;
        surfaceDesc.planeVerticalOffsetMeters = options.planeVerticalOffsetMeters;
        surfaceDesc.placementMode = options.placement == Vdca::PlanePlacement::HeadRelative
            ? VarjoXR::PlacementMode::HeadRelative
            : VarjoXR::PlacementMode::World;

        Vdca::StereoPlaneSurface surface(core, d3d12Backend, space, surfaceDesc);

        // Future calibration integration point:
        //   surface.setProcessing(VarjoXR::Eye::Left, leftRemapProcessing);
        //   surface.setProcessing(VarjoXR::Eye::Right, rightRemapProcessing);
        // The capture/sync/copy path remains unchanged.
        surface.updateFromSynchronizedFrame(*firstFrame);
        firstFrame.reset();

        LogInfo(L"rendering synchronized stereo frames on one VarjoXR Plane");
        LogInfo(L"press Esc or Ctrl+C to exit");

        const auto startTime = std::chrono::steady_clock::now();
        std::uint64_t renderedFrames = 0;
        std::uint64_t lastLoggedPair = 0;

        while (!gStopRequested.load(std::memory_order_relaxed)) {
            if ((GetAsyncKeyState(VK_ESCAPE) & 0x1) != 0) {
                break;
            }

            if (auto latest = synchronizedQueue->tryPopLatest()) {
                surface.updateFromSynchronizedFrame(*latest);
            }

            const auto now = std::chrono::steady_clock::now();
            space.frameContext().timeSeconds = std::chrono::duration<double>(now - startTime).count();
            space.frameContext().frameNumber = ++renderedFrames;
            space.update();

            if ((renderedFrames % 120u) == 0u) {
                RethrowWorkerExceptions(leftCapture, rightCapture, sync);
            }

            if (options.logEveryPairs != 0 &&
                surface.lastPairNumber() != lastLoggedPair &&
                (surface.lastPairNumber() % options.logEveryPairs) == 0u) {
                PrintPeriodicStats(leftCapture, rightCapture, sync, surface);
                lastLoggedPair = surface.lastPairNumber();
            }
        }

        LogInfo(L"stopping capture and synchronization threads");
        stopGuard.stop();
        core->WaitIdle();

        const auto finalStats = sync.stats();
        std::wcout << L"[INFO] final sync stats: pairs=" << finalStats.pairsOut
                   << L", leftIn=" << finalStats.leftFramesIn
                   << L", rightIn=" << finalStats.rightFramesIn
                   << L", droppedLeft=" << finalStats.droppedLeft
                   << L", droppedRight=" << finalStats.droppedRight
                   << L", rejected=" << finalStats.rejectedPairs << L'\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[ERROR] " << error.what() << '\n';
        LogError(L"application terminated; inspect the preceding capture/Varjo/D3D12 logs");
        return 1;
    }
}
