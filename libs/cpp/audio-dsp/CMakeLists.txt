# Deterministic signal processing for derived streams (issue #15, master
# prompt §7): stereo downmix, anti-aliased decimation, and the 16 kHz mono STT
# stream producer. Platform-neutral; no third-party dependencies.
add_library(voxmesh-audio-dsp STATIC
    src/fir_decimator.cpp
    src/stt_stream_producer.cpp)
add_library(voxmesh::audio-dsp ALIAS voxmesh-audio-dsp)

target_include_directories(voxmesh-audio-dsp PUBLIC include)
target_link_libraries(voxmesh-audio-dsp
    PUBLIC
        voxmesh::audio-core
    PRIVATE
        voxmesh::build_flags)

if(VOXMESH_BUILD_TESTS)
    add_subdirectory(tests)
endif()
