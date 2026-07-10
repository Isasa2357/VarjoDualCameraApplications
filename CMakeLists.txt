cmake_minimum_required(VERSION 3.20)

project(VarjoDualCameraApplications
    VERSION 0.1.0
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "VarjoDualCameraApplications requires Windows, Direct3D 12, and Varjo Runtime.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VDCA_BUILD_MF_FRAME_SYNC_TO_VARJO_PLANE
    "Build the MFFrameSource stereo synchronization to VarjoXR Plane application"
    ON)

include(cmake/Dependencies.cmake)
vdca_resolve_dependencies()

if(VDCA_BUILD_MF_FRAME_SYNC_TO_VARJO_PLANE)
    add_subdirectory(apps/MFFrameSyncToVarjoPlane)
endif()
