set(target_name MFFrameSyncToVarjoPlane)

add_executable(${target_name}
    src/AppOptions.cpp
    src/AppOptions.hpp
    src/StereoPlaneSurface.cpp
    src/StereoPlaneSurface.hpp
    src/StereoRectification.cpp
    src/StereoRectification.hpp
    src/main.cpp
)

target_compile_features(${target_name} PRIVATE cxx_std_17)

target_link_libraries(${target_name}
    PRIVATE
        MFFrameSource::D3D12
        VarjoXR::VarjoXR
        D3D12Helper::D3D12Helper
        nlohmann_json::nlohmann_json
)

target_compile_definitions(${target_name}
    PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
)

if(MSVC)
    target_compile_options(${target_name} PRIVATE /utf-8 /EHsc /W4 /permissive-)
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source" FILES
    src/AppOptions.cpp
    src/AppOptions.hpp
    src/StereoPlaneSurface.cpp
    src/StereoPlaneSurface.hpp
    src/StereoRectification.cpp
    src/StereoRectification.hpp
    src/main.cpp)

if(COMMAND varjoxr_copy_varjo_runtime)
    varjoxr_copy_varjo_runtime(${target_name})
endif()

if(COMMAND varjoxr_copy_dxc_runtime)
    varjoxr_copy_dxc_runtime(${target_name})
endif()

if(VDCA_D3D12HELPER_SOURCE_DIR AND
   EXISTS "${VDCA_D3D12HELPER_SOURCE_DIR}/shaders/D3D12Processing")
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${target_name}>/shaders/D3D12Processing"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${VDCA_D3D12HELPER_SOURCE_DIR}/shaders/D3D12Processing"
            "$<TARGET_FILE_DIR:${target_name}>/shaders/D3D12Processing"
        COMMENT "Copying D3D12Processing shaders"
        VERBATIM)
else()
    message(WARNING
        "D3D12Processing shader source directory is unknown. "
        "Pass --shader-dir at runtime if MFFrameSource cannot find its shaders.")
endif()
