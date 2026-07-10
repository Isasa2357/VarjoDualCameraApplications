find_package(OpenCV REQUIRED COMPONENTS core imgproc calib3d)

add_library(VdcaStereoCalibrationOpenCV STATIC
    src/CalibrationAlgorithms.cpp
    src/CalibrationAlgorithms.hpp
    src/D3D12StereoReadback.cpp
    src/D3D12StereoReadback.hpp
    src/RealtimeStereoCalibrator.cpp
)
add_library(Vdca::StereoCalibrationOpenCV ALIAS VdcaStereoCalibrationOpenCV)

target_compile_features(VdcaStereoCalibrationOpenCV PUBLIC cxx_std_17)
target_include_directories(VdcaStereoCalibrationOpenCV
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(VdcaStereoCalibrationOpenCV
    PUBLIC
        Vdca::StereoCalibrationCore
        D3D12Helper::D3D12Helper
    PRIVATE
        ${OpenCV_LIBS}
)
target_compile_definitions(VdcaStereoCalibrationOpenCV PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
    target_compile_options(VdcaStereoCalibrationOpenCV PRIVATE /utf-8 /EHsc /W4 /permissive-)
endif()
