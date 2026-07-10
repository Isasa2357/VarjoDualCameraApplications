add_library(VdcaStereoCalibrationCore STATIC
    src/CalibrationDocument.cpp
    src/D3D12StereoFrame.cpp
    src/CalibrationSnapshotStore.cpp
)
add_library(Vdca::StereoCalibrationCore ALIAS VdcaStereoCalibrationCore)

target_compile_features(VdcaStereoCalibrationCore PUBLIC cxx_std_17)
target_include_directories(VdcaStereoCalibrationCore
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(VdcaStereoCalibrationCore
    PUBLIC
        nlohmann_json::nlohmann_json
)
target_compile_definitions(VdcaStereoCalibrationCore PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
    target_compile_options(VdcaStereoCalibrationCore PRIVATE /utf-8 /EHsc /W4 /permissive-)
endif()
