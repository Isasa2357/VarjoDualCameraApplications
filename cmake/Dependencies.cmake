include(FetchContent)

# This repository consumes dependencies as source subprojects and does not
# generate install packages. Some dependencies export external targets from
# install(EXPORT), which fails when those targets are provided by sibling
# FetchContent projects. Disable install-rule generation for the superbuild.
set(CMAKE_SKIP_INSTALL_RULES ON CACHE BOOL
    "Disable install rules for the VarjoDualCameraApplications superbuild" FORCE)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL
    "Do not update already-populated FetchContent dependencies automatically" FORCE)

set(D3D12HELPER_ROOT "" CACHE PATH "Optional local D3D12Helper repository root")
set(VARJOXR_ROOT "" CACHE PATH "Optional local VarjoXR repository root")
set(MFFRAMESOURCE_ROOT "" CACHE PATH "Optional local MFFrameSource repository root")
set(THREADKIT_ROOT "" CACHE PATH "Optional local ThreadKit repository root forwarded to MFFrameSource")
set(NLOHMANN_JSON_ROOT "" CACHE PATH "Optional local nlohmann/json repository root")

set(VDCA_D3D12HELPER_GIT_REPOSITORY
    "https://github.com/Isasa2357/D3D12Helper.git"
    CACHE STRING "D3D12Helper Git repository")
set(VDCA_D3D12HELPER_GIT_TAG
    "v1.13.0"
    CACHE STRING "D3D12Helper Git ref/tag/commit")

set(VDCA_VARJOXR_GIT_REPOSITORY
    "https://github.com/Isasa2357/VarjoXR.git"
    CACHE STRING "VarjoXR Git repository")
set(VDCA_VARJOXR_GIT_TAG
    "main"
    CACHE STRING "VarjoXR Git ref/tag/commit")

set(VDCA_MFFRAMESOURCE_GIT_REPOSITORY
    "https://github.com/Isasa2357/MFFrameSource.git"
    CACHE STRING "MFFrameSource Git repository")
set(VDCA_MFFRAMESOURCE_GIT_TAG
    "main"
    CACHE STRING "MFFrameSource Git ref/tag/commit")

set(VDCA_NLOHMANN_JSON_GIT_REPOSITORY
    "https://github.com/nlohmann/json.git"
    CACHE STRING "nlohmann/json Git repository")
set(VDCA_NLOHMANN_JSON_GIT_TAG
    "v3.11.3"
    CACHE STRING "nlohmann/json Git ref/tag/commit")

function(vdca_resolve_d3d12helper)
    if(TARGET D3D12Helper::D3D12Helper)
        return()
    endif()

    set(D3D12HELPER_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    set(D3D12HELPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(D3D12HELPER_INSTALL OFF CACHE BOOL "" FORCE)
    set(D3D12HELPER_ENABLE_PACKAGE_SMOKE_TESTS OFF CACHE BOOL "" FORCE)

    if(D3D12HELPER_ROOT AND EXISTS "${D3D12HELPER_ROOT}/CMakeLists.txt")
        message(STATUS "VDCA: using local D3D12Helper: ${D3D12HELPER_ROOT}")
        add_subdirectory(
            "${D3D12HELPER_ROOT}"
            "${CMAKE_BINARY_DIR}/_deps/D3D12Helper-build")
        set(VDCA_D3D12HELPER_SOURCE_DIR "${D3D12HELPER_ROOT}" CACHE INTERNAL "" FORCE)
    else()
        FetchContent_Declare(
            VDCA_D3D12Helper
            GIT_REPOSITORY "${VDCA_D3D12HELPER_GIT_REPOSITORY}"
            GIT_TAG        "${VDCA_D3D12HELPER_GIT_TAG}"
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(VDCA_D3D12Helper)
        FetchContent_GetProperties(VDCA_D3D12Helper)
        set(VDCA_D3D12HELPER_SOURCE_DIR "${vdca_d3d12helper_SOURCE_DIR}" CACHE INTERNAL "" FORCE)
    endif()
endfunction()

function(vdca_resolve_varjoxr)
    if(TARGET VarjoXR::VarjoXR)
        return()
    endif()

    set(VARJOXR_ENABLE_D3D11 OFF CACHE BOOL "" FORCE)
    set(VARJOXR_ENABLE_D3D12 ON CACHE BOOL "" FORCE)
    set(VARJOXR_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    set(VARJOXR_BUILD_TESTS OFF CACHE BOOL "" FORCE)

    # D3D12Helper is deliberately resolved before VarjoXR so VarjoXR and
    # MFFrameSource share one D3D12Helper target and one D3D12Core type.
    if(VARJOXR_ROOT AND EXISTS "${VARJOXR_ROOT}/CMakeLists.txt")
        message(STATUS "VDCA: using local VarjoXR: ${VARJOXR_ROOT}")
        add_subdirectory(
            "${VARJOXR_ROOT}"
            "${CMAKE_BINARY_DIR}/_deps/VarjoXR-build")
    else()
        FetchContent_Declare(
            VDCA_VarjoXR
            GIT_REPOSITORY "${VDCA_VARJOXR_GIT_REPOSITORY}"
            GIT_TAG        "${VDCA_VARJOXR_GIT_TAG}"
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(VDCA_VarjoXR)
    endif()
endfunction()

function(vdca_resolve_mfframesource)
    if(TARGET MFFrameSource::D3D12)
        return()
    endif()

    set(MFFRAMESOURCE_BUILD_SAMPLE OFF CACHE BOOL "" FORCE)
    set(MFFRAMESOURCE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(MFFRAMESOURCE_FETCH_D3D12HELPER OFF CACHE BOOL "" FORCE)

    if(MFFRAMESOURCE_ROOT AND EXISTS "${MFFRAMESOURCE_ROOT}/CMakeLists.txt")
        message(STATUS "VDCA: using local MFFrameSource: ${MFFRAMESOURCE_ROOT}")
        add_subdirectory(
            "${MFFRAMESOURCE_ROOT}"
            "${CMAKE_BINARY_DIR}/_deps/MFFrameSource-build")
    else()
        FetchContent_Declare(
            VDCA_MFFrameSource
            GIT_REPOSITORY "${VDCA_MFFRAMESOURCE_GIT_REPOSITORY}"
            GIT_TAG        "${VDCA_MFFRAMESOURCE_GIT_TAG}"
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(VDCA_MFFrameSource)
    endif()
endfunction()

function(vdca_resolve_nlohmann_json)
    if(TARGET nlohmann_json::nlohmann_json)
        return()
    endif()

    find_package(nlohmann_json 3.11.3 CONFIG QUIET)
    if(TARGET nlohmann_json::nlohmann_json)
        return()
    endif()

    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    set(JSON_Install OFF CACHE BOOL "" FORCE)

    if(NLOHMANN_JSON_ROOT AND EXISTS "${NLOHMANN_JSON_ROOT}/CMakeLists.txt")
        message(STATUS "VDCA: using local nlohmann/json: ${NLOHMANN_JSON_ROOT}")
        add_subdirectory(
            "${NLOHMANN_JSON_ROOT}"
            "${CMAKE_BINARY_DIR}/_deps/nlohmann-json-build")
    else()
        FetchContent_Declare(
            VDCA_nlohmann_json
            GIT_REPOSITORY "${VDCA_NLOHMANN_JSON_GIT_REPOSITORY}"
            GIT_TAG        "${VDCA_NLOHMANN_JSON_GIT_TAG}"
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(VDCA_nlohmann_json)
    endif()
endfunction()

function(vdca_resolve_dependencies)
    vdca_resolve_d3d12helper()
    vdca_resolve_varjoxr()
    vdca_resolve_mfframesource()
    vdca_resolve_nlohmann_json()

    foreach(required_target IN ITEMS
        D3D12Helper::D3D12Helper
        VarjoXR::VarjoXR
        MFFrameSource::D3D12
        nlohmann_json::nlohmann_json)
        if(NOT TARGET ${required_target})
            message(FATAL_ERROR "VDCA: required target was not created: ${required_target}")
        endif()
    endforeach()
endfunction()
