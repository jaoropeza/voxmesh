# Minimal Phase 0 shell: proves the Qt/QML/CMake/packaging path end to end.
# No audio capture here — that arrives with Phase 1 (audio-core) and Phase 2 (WASAPI).
find_package(Qt6 6.5 QUIET COMPONENTS Core Gui Qml Quick)

if(NOT Qt6_FOUND)
    if(VOXMESH_REQUIRE_QT)
        message(FATAL_ERROR "Qt 6.5+ not found and VOXMESH_REQUIRE_QT=ON.")
    endif()
    message(STATUS "Qt 6 not found — skipping desktop-recorder target.")
    return()
endif()

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(voxmesh-recorder
    src/main.cpp)

qt_add_qml_module(voxmesh-recorder
    URI VoxMesh.Recorder
    VERSION 1.0
    QML_FILES
        qml/Main.qml)

target_link_libraries(voxmesh-recorder PRIVATE
    voxmesh::core
    voxmesh::build_flags
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick)
