if(NOT WIN32)
    return()
endif()

add_library(voxmesh-audio-wasapi STATIC
    src/com_utils.cpp
    src/wasapi_backend.cpp
    src/wasapi_capture_stream.cpp
    src/wasapi_device_enumerator.cpp)
add_library(voxmesh::audio-wasapi ALIAS voxmesh-audio-wasapi)

target_include_directories(voxmesh-audio-wasapi PUBLIC include)
target_compile_definitions(voxmesh-audio-wasapi PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE)
target_link_libraries(voxmesh-audio-wasapi
    PUBLIC voxmesh::audio-core
    PRIVATE
        voxmesh::build_flags
        ole32
        avrt)

if(VOXMESH_BUILD_TESTS)
    add_subdirectory(tests)
endif()
