cmake_minimum_required(VERSION 3.28)

project(voxmesh
    VERSION 0.1.0
    DESCRIPTION "VoxMesh meeting recording platform"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed; use a preset (ADR-0005).")
endif()

option(VOXMESH_BUILD_TESTS "Build C++ unit tests" ON)
option(VOXMESH_BUILD_BENCHMARKS "Build performance benchmarks" ON)
option(VOXMESH_REQUIRE_QT "Fail configuration when Qt 6 is not found (CI)" OFF)
option(VOXMESH_WARNINGS_AS_ERRORS "Treat warnings as errors for project code" ON)

# Strict warnings for project-owned code only; third-party targets never link this.
add_library(voxmesh_build_flags INTERFACE)
add_library(voxmesh::build_flags ALIAS voxmesh_build_flags)
if(MSVC)
    target_compile_options(voxmesh_build_flags INTERFACE /W4 /permissive- /utf-8)
    if(VOXMESH_WARNINGS_AS_ERRORS)
        target_compile_options(voxmesh_build_flags INTERFACE /WX)
    endif()
else()
    target_compile_options(voxmesh_build_flags INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(VOXMESH_WARNINGS_AS_ERRORS)
        target_compile_options(voxmesh_build_flags INTERFACE -Werror)
    endif()
endif()

if(VOXMESH_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    include(GoogleTest)
endif()

add_subdirectory(libs/cpp)
add_subdirectory(apps/desktop-recorder)
