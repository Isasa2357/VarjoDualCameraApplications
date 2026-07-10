add_library(VdcaVarjoStereoView STATIC
    src/VarjoStereoView.cpp
)
add_library(Vdca::VarjoStereoView ALIAS VdcaVarjoStereoView)

target_compile_features(VdcaVarjoStereoView PUBLIC cxx_std_17)
target_include_directories(VdcaVarjoStereoView
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(VdcaVarjoStereoView
    PUBLIC
        Vdca::StereoCalibrationCore
        VarjoXR::VarjoXR
        D3D12Helper::D3D12Helper
)
target_compile_definitions(VdcaVarjoStereoView PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
    target_compile_options(VdcaVarjoStereoView PRIVATE /utf-8 /EHsc /W4 /permissive-)
endif()
