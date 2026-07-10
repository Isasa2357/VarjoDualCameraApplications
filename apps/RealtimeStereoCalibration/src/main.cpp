#include "AppOptions.hpp"

#include <VdcaStereoCalibration/CalibrationDocument.hpp>
#include <VdcaStereoCalibration/RealtimeStereoCalibrator.hpp>
#include <VdcaVarjoStereoView/VarjoStereoView.hpp>

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
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace {

namespace App = Vdca::RealtimeCalibrationApp;
namespace Calibration = Vdca::StereoCalibration;
namespace View = Vdca::VarjoStereoView;

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

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) throw std::runtime_error("failed to convert UTF-8 to UTF-16");
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), count) != count) {
        throw std::runtime_error("failed to convert UTF-8 to UTF-16");
    }
    return result;
}

std::filesystem::path ExecutableDirectory() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) throw std::runtime_error("GetModuleFileNameW failed");
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path ResolveShaderDirectory(const App::AppOptions& options) {
    if (!options.shaderDirectory.empty()) {
        if (!std::filesystem::is_directory(options.shaderDirectory)) {
            throw std::runtime_error("--shader-dir does not point to a directory");
        }
        return std::filesystem::absolute(options.shaderDirectory);
    }
    const auto bundled = ExecutableDirectory() / L"shaders" / L"D3D12Processing";
    return std::filesystem::is_directory(bundled) ? bundled : std::filesystem::path{};
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
    const App::AppOptions& options,
    Calibration::ImageSize processingSize,
    const std::filesystem::path& shaderDirectory) {
    MFFrameSource::MFD3D12CameraCaptureThreadConfig config;
    config.selector.deviceIndex = cameraIndex;
    config.capture.input.width = options.width;
    config.capture.input.height = options.height;
    config.capture.input.fps.numerator = options.fpsNumerator;
    config.capture.input.fps.denominator = options.fpsDenominator;
    config.capture.input.subtype = ParseSubtype(options.subtype);
    config.capture.outputWidth = processingSize.width;
    config.capture.outputHeight = processingSize.height;
    config.capture.outputFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    config.capture.processingShaderDirectory = shaderDirectory.wstring();
    config.capture.framePoolSize = 4;
    config.capture.waitForGpuCompletionOnRead = false;
    config.defaultQueueCapacity = std::max(options.displayCaptureQueueCapacity, options.calibrationCaptureQueueCapacity);
    config.clonePoolSize = std::max<std::size_t>(
        16,
        options.syncCandidateCapacity * 2u +
            options.displayCaptureQueueCapacity +
            options.calibrationCaptureQueueCapacity + 4u);
    config.clonePersistentSrvDescriptorCount = static_cast<UINT>(config.clonePoolSize);
    return config;
}

void ThrowCameraOpenError(
    const wchar_t* eye,
    const MFFrameSource::MFD3D12CameraCaptureThread& capture) {
    const auto& error = capture.lastError();
    std::wstring message = eye;
    message += L" camera open failed";
    if (!error.where.empty()) message += L" at " + error.where;
    if (!error.message.empty()) message += L": " + error.message;
    LogError(message);
    throw std::runtime_error("camera open failed; inspect the detailed log");
}

std::shared_ptr<VarjoSession> CreateVarjoSession() {
    auto session = std::make_shared<VarjoSession>();
    if (!session->valid() && !session->initialize()) {
        throw std::runtime_error("failed to initialize Varjo session: " + session->lastError());
    }
    return session;
}

Calibration::StereoD3D12Frame MakeGenericFrame(
    std::shared_ptr<MFFrameSource::MFD3D12StereoFrame> owner) {
    if (!owner || !*owner) throw std::invalid_argument("MakeGenericFrame: empty synchronized frame");
    Calibration::StereoD3D12Frame result;
    result.left = Calibration::MakeD3D12ImageFrame(
        owner->left.resource().Get(),
        owner->left.resourceState());
    result.right = Calibration::MakeD3D12ImageFrame(
        owner->right.resource().Get(),
        owner->right.resourceState());
    result.frameNumber = owner->pairNumber;
    result.leftTimestamp100ns = owner->left.sampleTime100ns();
    result.rightTimestamp100ns = owner->right.sampleTime100ns();
    result.lifetimeToken = std::move(owner);
    return result;
}

class PipelineStopGuard {
public:
    PipelineStopGuard(
        MFFrameSource::MFD3D12CameraCaptureThread& left,
        MFFrameSource::MFD3D12CameraCaptureThread& right,
        MFFrameSource::MFD3D12CameraSyncThread& displaySync,
        MFFrameSource::MFD3D12CameraSyncThread& calibrationSync)
        : left_(left), right_(right), displaySync_(displaySync), calibrationSync_(calibrationSync) {}

    ~PipelineStopGuard() { stop(); }

    void stop() noexcept {
        if (stopped_) return;
        stopped_ = true;
        try { displaySync_.stop(); } catch (...) {}
        try { calibrationSync_.stop(); } catch (...) {}
        try { left_.stop(); } catch (...) {}
        try { right_.stop(); } catch (...) {}
    }

private:
    MFFrameSource::MFD3D12CameraCaptureThread& left_;
    MFFrameSource::MFD3D12CameraCaptureThread& right_;
    MFFrameSource::MFD3D12CameraSyncThread& displaySync_;
    MFFrameSource::MFD3D12CameraSyncThread& calibrationSync_;
    bool stopped_ = false;
};

class CalibrationFeedWorker {
public:
    CalibrationFeedWorker(
        MFFrameSource::MFD3D12StereoFrameQueuePtr queue,
        Calibration::RealtimeStereoCalibrator& calibrator)
        : queue_(std::move(queue)), calibrator_(calibrator) {}

    ~CalibrationFeedWorker() { stop(); }

    void start() {
        if (thread_.joinable()) return;
        stopRequested_.store(false);
        thread_ = std::thread([this] { run(); });
    }

    void requestStop() noexcept { stopRequested_.store(true); }

    void stop() noexcept {
        requestStop();
        if (thread_.joinable()) thread_.join();
    }

    void rethrowIfAny() const {
        std::lock_guard<std::mutex> lock(exceptionMutex_);
        if (exception_) std::rethrow_exception(exception_);
    }

private:
    void run() noexcept {
        try {
            while (!stopRequested_.load()) {
                auto pair = queue_->waitPopLatestFor(std::chrono::milliseconds(100));
                if (!pair) continue;
                auto owner = std::make_shared<MFFrameSource::MFD3D12StereoFrame>(std::move(*pair));
                calibrator_.submitLatestFrame(MakeGenericFrame(std::move(owner)));
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(exceptionMutex_);
            exception_ = std::current_exception();
        }
    }

    MFFrameSource::MFD3D12StereoFrameQueuePtr queue_;
    Calibration::RealtimeStereoCalibrator& calibrator_;
    std::atomic_bool stopRequested_{false};
    std::thread thread_;
    mutable std::mutex exceptionMutex_;
    std::exception_ptr exception_;
};

void RethrowPipelineExceptions(
    const MFFrameSource::MFD3D12CameraCaptureThread& left,
    const MFFrameSource::MFD3D12CameraCaptureThread& right,
    const MFFrameSource::MFD3D12CameraSyncThread& displaySync,
    const MFFrameSource::MFD3D12CameraSyncThread& calibrationSync,
    const CalibrationFeedWorker& feeder,
    const Calibration::RealtimeStereoCalibrator& calibrator) {
    left.rethrowWorkerExceptionIfAny();
    right.rethrowWorkerExceptionIfAny();
    displaySync.rethrowWorkerExceptionIfAny();
    calibrationSync.rethrowWorkerExceptionIfAny();
    feeder.rethrowIfAny();
    calibrator.rethrowWorkerExceptionIfAny();
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        const App::AppOptions options = App::ParseAppOptions(argc, argv);
        if (options.showHelp) {
            App::PrintUsage(std::wcout);
            return 0;
        }
        SetConsoleCtrlHandler(ConsoleControlHandler, TRUE);

        std::optional<Calibration::CalibrationDocument> initialDocument;
        if (!options.initialJson.empty()) {
            const auto path = std::filesystem::absolute(options.initialJson);
            initialDocument = Calibration::CalibrationDocument::loadJson(path);
            LogInfo(L"loaded initial calibration: " + path.wstring());
        }

        const Calibration::ImageSize sourceSize{options.width, options.height};
        Calibration::ImageSize processingSize{
            options.processingWidth ? options.processingWidth : options.width,
            options.processingHeight ? options.processingHeight : options.height,
        };
        Calibration::ImageSize outputSize{
            options.outputWidth ? options.outputWidth : processingSize.width,
            options.outputHeight ? options.outputHeight : processingSize.height,
        };
        std::string activeProfile = options.activeProfile.empty() ? "uncalibrated" : options.activeProfile;
        std::string rightOrder = options.rightOrder.empty() ? "same" : options.rightOrder;

        if (initialDocument) {
            if (initialDocument->sourceSize.width != sourceSize.width || initialDocument->sourceSize.height != sourceSize.height) {
                throw std::invalid_argument("initial JSON source_size does not match --width/--height");
            }
            if (options.processingWidth != 0 &&
                (initialDocument->calibrationInputSize.width != processingSize.width ||
                 initialDocument->calibrationInputSize.height != processingSize.height)) {
                throw std::invalid_argument("explicit processing size conflicts with initial JSON");
            }
            if (options.outputWidth != 0 &&
                (initialDocument->rectifiedOutputSize.width != outputSize.width ||
                 initialDocument->rectifiedOutputSize.height != outputSize.height)) {
                throw std::invalid_argument("explicit output size conflicts with initial JSON");
            }
            processingSize = initialDocument->calibrationInputSize;
            outputSize = initialDocument->rectifiedOutputSize;
            if (options.activeProfile.empty()) activeProfile = initialDocument->defaultProfile;
            if (options.rightOrder.empty()) rightOrder = initialDocument->rightOrder;
            if (!initialDocument->hasProfile(activeProfile)) {
                throw std::invalid_argument("initial JSON does not contain the selected profile");
            }
        }

        LogInfo(
            L"pipeline geometry: native " + std::to_wstring(sourceSize.width) + L"x" + std::to_wstring(sourceSize.height) +
            L" -> analysis/display input " + std::to_wstring(processingSize.width) + L"x" + std::to_wstring(processingSize.height) +
            L" -> rectified output " + std::to_wstring(outputSize.width) + L"x" + std::to_wstring(outputSize.height));
        LogInfo(L"active profile: " + Utf8ToWide(activeProfile));

        MFFrameSource::MFPlatformContext mediaFoundation;
        if (!mediaFoundation.initialized()) throw std::runtime_error("Media Foundation initialization failed");

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
        MFFrameSource::MFD3D12CameraSyncThread displaySync;
        MFFrameSource::MFD3D12CameraSyncThread calibrationSync;
        PipelineStopGuard pipelineGuard(leftCapture, rightCapture, displaySync, calibrationSync);

        const auto leftConfig = MakeCaptureConfig(options.leftCameraIndex, options, processingSize, shaderDirectory);
        const auto rightConfig = MakeCaptureConfig(options.rightCameraIndex, options, processingSize, shaderDirectory);
        if (!leftCapture.open(leftConfig, core)) ThrowCameraOpenError(L"left", leftCapture);
        if (!rightCapture.open(rightConfig, core)) ThrowCameraOpenError(L"right", rightCapture);

        auto leftDisplayQueue = leftCapture.createQueue(options.displayCaptureQueueCapacity);
        auto rightDisplayQueue = rightCapture.createQueue(options.displayCaptureQueueCapacity);
        auto leftCalibrationQueue = leftCapture.createQueue(options.calibrationCaptureQueueCapacity);
        auto rightCalibrationQueue = rightCapture.createQueue(options.calibrationCaptureQueueCapacity);

        MFFrameSource::MFD3D12CameraSyncThreadConfig displaySyncConfig;
        displaySyncConfig.maxAdjustedDiff100ns = options.syncToleranceMicroseconds * 10;
        displaySyncConfig.candidateCapacity = options.syncCandidateCapacity;
        displaySyncConfig.outputQueueCapacity = options.displaySyncOutputCapacity;
        displaySyncConfig.outputOverflowPolicy = ThreadKit::Queues::QueueOverflowPolicy::DropOldest;

        MFFrameSource::MFD3D12CameraSyncThreadConfig calibrationSyncConfig = displaySyncConfig;
        calibrationSyncConfig.outputQueueCapacity = options.calibrationSyncOutputCapacity;

        if (!displaySync.open(leftDisplayQueue, rightDisplayQueue, displaySyncConfig)) {
            throw std::runtime_error("display FrameSyncThread open failed");
        }
        if (!calibrationSync.open(leftCalibrationQueue, rightCalibrationQueue, calibrationSyncConfig)) {
            throw std::runtime_error("calibration FrameSyncThread open failed");
        }

        Calibration::RealtimeStereoCalibratorConfig calibratorConfig;
        calibratorConfig.d3d12 = core;
        calibratorConfig.sourceSize = sourceSize;
        calibratorConfig.processingInputSize = processingSize;
        calibratorConfig.rectifiedOutputSize = outputSize;
        calibratorConfig.boardColumns = options.boardColumns;
        calibratorConfig.boardRows = options.boardRows;
        calibratorConfig.rightOrder = rightOrder;
        calibratorConfig.activeProfile = activeProfile;
        calibratorConfig.maxObservationCount = options.maxObservations;
        calibratorConfig.minObservationCountForUpdate = options.minObservations;
        calibratorConfig.minMeanCornerMotionPx = options.minCornerMotionPx;
        calibratorConfig.fundamentalRansacThresholdPx = options.ransacThresholdPx;
        calibratorConfig.fitUncalibratedResultToCanvas = options.fitCanvas;
        calibratorConfig.useFindChessboardCornersSB = options.useChessboardSb;
        calibratorConfig.initialCalibration = initialDocument;
        Calibration::RealtimeStereoCalibrator calibrator(std::move(calibratorConfig));
        calibrator.start();

        CalibrationFeedWorker calibrationFeeder(calibrationSync.outputQueue(), calibrator);
        calibrationFeeder.start();

        displaySync.start();
        calibrationSync.start();
        leftCapture.start();
        rightCapture.start();
        LogInfo(L"two capture threads and two independent FrameSyncThread pipelines started");

        auto firstDisplayPair = displaySync.outputQueue()->waitPopLatestFor(
            std::chrono::milliseconds(options.startupTimeoutMilliseconds));
        if (!firstDisplayPair) {
            RethrowPipelineExceptions(leftCapture, rightCapture, displaySync, calibrationSync, calibrationFeeder, calibrator);
            throw std::runtime_error("timed out waiting for the first display stereo frame");
        }

        View::ViewConfig viewConfig;
        viewConfig.inputSize = processingSize;
        viewConfig.rectifiedOutputSize = outputSize;
        viewConfig.format = firstDisplayPair->left.format();
        viewConfig.planeWidthMeters = options.planeWidthMeters;
        viewConfig.planeDistanceMeters = options.planeDistanceMeters;
        viewConfig.planeVerticalOffsetMeters = options.planeVerticalOffsetMeters;
        viewConfig.placementMode = options.placement == App::PlanePlacement::HeadRelative
            ? VarjoXR::PlacementMode::HeadRelative
            : VarjoXR::PlacementMode::World;
        View::VarjoStereoView view(core, d3d12Backend, space, viewConfig);

        auto snapshot = calibrator.latestSnapshot();
        if (!snapshot) throw std::runtime_error("calibrator did not publish its initial snapshot");
        view.applyCalibration(*snapshot);
        std::uint64_t lastSavedRevision = 0;
        if (!options.outputJson.empty()) {
            snapshot->document->saveJsonAtomically(std::filesystem::absolute(options.outputJson));
            lastSavedRevision = snapshot->revision;
        }

        auto firstOwner = std::make_shared<MFFrameSource::MFD3D12StereoFrame>(std::move(*firstDisplayPair));
        view.submitFrame(MakeGenericFrame(std::move(firstOwner)));

        LogInfo(L"show the checkerboard at varied positions and orientations; accepted estimates are applied live");
        LogInfo(L"press Esc or Ctrl+C to exit");

        const auto startTime = std::chrono::steady_clock::now();
        std::uint64_t renderedFrames = 0;
        while (!gStopRequested.load(std::memory_order_relaxed)) {
            if ((GetAsyncKeyState(VK_ESCAPE) & 0x1) != 0) break;

            if (auto latest = displaySync.outputQueue()->tryPopLatest()) {
                auto owner = std::make_shared<MFFrameSource::MFD3D12StereoFrame>(std::move(*latest));
                view.submitFrame(MakeGenericFrame(std::move(owner)));
            }

            if (auto latestCalibration = calibrator.latestSnapshot();
                latestCalibration && latestCalibration->revision != view.appliedCalibrationRevision()) {
                view.applyCalibration(*latestCalibration);
                const auto& quality = latestCalibration->document->profile(latestCalibration->activeProfile).quality;
                std::wcout << L"[CALIB] revision=" << latestCalibration->revision
                           << L" profile=" << Utf8ToWide(latestCalibration->activeProfile)
                           << L" observations=" << quality.usedPairs;
                if (quality.medianAbsVerticalErrorPx) {
                    std::wcout << L" medianVerticalError=" << std::fixed << std::setprecision(4)
                               << *quality.medianAbsVerticalErrorPx << L"px";
                }
                std::wcout << L'\n';

                if (!options.outputJson.empty() && latestCalibration->revision != lastSavedRevision) {
                    try {
                        latestCalibration->document->saveJsonAtomically(std::filesystem::absolute(options.outputJson));
                        lastSavedRevision = latestCalibration->revision;
                    } catch (const std::exception& error) {
                        std::cerr << "[WARN] failed to save calibration JSON: " << error.what() << '\n';
                    }
                }
            }

            const auto now = std::chrono::steady_clock::now();
            space.frameContext().timeSeconds = std::chrono::duration<double>(now - startTime).count();
            space.frameContext().frameNumber = ++renderedFrames;
            space.update();

            if ((renderedFrames % 120u) == 0u) {
                RethrowPipelineExceptions(leftCapture, rightCapture, displaySync, calibrationSync, calibrationFeeder, calibrator);
            }
            if (options.logEveryFrames != 0 && (renderedFrames % options.logEveryFrames) == 0u) {
                const auto stats = calibrator.stats();
                const auto displayStats = displaySync.stats();
                const auto calculationStats = calibrationSync.stats();
                std::wcout << L"[STATS] displayPairs=" << displayStats.pairsOut
                           << L" calculationPairs=" << calculationStats.pairsOut
                           << L" analyzed=" << stats.analyzedFrames
                           << L" accepted=" << stats.acceptedObservations
                           << L" checkerboardMisses=" << stats.checkerboardMisses
                           << L" pendingReplaced=" << stats.replacedPendingFrames
                           << L" revisions=" << stats.publishedRevisions << L'\n';
            }
        }

        LogInfo(L"stopping realtime calibration pipeline");
        pipelineGuard.stop();
        calibrationFeeder.stop();
        calibrator.stop();
        core->WaitIdle();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[ERROR] " << error.what() << '\n';
        LogError(L"RealtimeStereoCalibration terminated; inspect preceding logs");
        return 1;
    }
}
