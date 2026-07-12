# Lossless recording writer over FFmpeg (issue #13, master prompt §7/§11).
# FFmpeg stays an implementation detail: consumers see only the
# voxmesh::audio::IRecordingWriter port and the factory in recording_writer.hpp.
find_package(ffmpeg REQUIRED)

add_library(voxmesh-media-container STATIC
    src/ffmpeg_recording_writer.cpp)
add_library(voxmesh::media-container ALIAS voxmesh-media-container)

target_include_directories(voxmesh-media-container PUBLIC include)
target_link_libraries(voxmesh-media-container
    PUBLIC
        voxmesh::audio-core
    PRIVATE
        voxmesh::build_flags
        ffmpeg::avformat
        ffmpeg::avcodec
        ffmpeg::avutil)
# avformat's network core references Winsock even with every network protocol
# disabled, but the Conan package only declares secur32/bcrypt/user32.
if(WIN32)
    target_link_libraries(voxmesh-media-container PRIVATE ws2_32)
endif()

if(VOXMESH_BUILD_TESTS)
    add_subdirectory(tests)
endif()
