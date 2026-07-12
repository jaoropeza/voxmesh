# Recorder UI (issues #14, #13): device selection, recording indicator,
# transport controls, and lossless FLAC/Matroska recording to disk.
find_package(Qt6 6.5 QUIET COMPONENTS Core Gui Qml Quick)

if(NOT Qt6_FOUND)
    if(VOXMESH_REQUIRE_QT)
        message(FATAL_ERROR "Qt 6.5+ not found and VOXMESH_REQUIRE_QT=ON.")
    endif()
    message(STATUS "Qt 6 not found — skipping desktop-recorder target.")
    return()
endif()

qt_standard_project_setup(REQUIRES 6.5)
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

# Controller/backend-factory library shared by the app and its tests.
add_library(voxmesh-recorder-core STATIC
    src/backend_factory.cpp
    src/backend_factory.hpp
    src/recorder_controller.cpp
    src/recorder_controller.hpp)
set_target_properties(voxmesh-recorder-core PROPERTIES AUTOMOC ON)
target_include_directories(voxmesh-recorder-core PUBLIC src)
target_link_libraries(voxmesh-recorder-core
    PUBLIC
        voxmesh::audio-core
        voxmesh::audio-dsp
        voxmesh::streaming-client
        Qt6::Core
    PRIVATE
        voxmesh::media-container
        voxmesh::build_flags)
if(WIN32)
    target_link_libraries(voxmesh-recorder-core PRIVATE voxmesh::audio-wasapi)
endif()

qt_add_executable(voxmesh-recorder
    src/main.cpp)

qt_add_qml_module(voxmesh-recorder
    URI VoxMesh.Recorder
    VERSION 1.0
    QML_FILES
        qml/Main.qml)

target_link_libraries(voxmesh-recorder PRIVATE
    voxmesh-recorder-core
    voxmesh::core
    voxmesh::build_flags
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick)

if(VOXMESH_BUILD_TESTS)
    add_subdirectory(tests)
endif()
