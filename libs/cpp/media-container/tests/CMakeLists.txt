add_executable(voxmesh-media-container-tests
    ffmpeg_recording_writer_test.cpp)

# Tests link FFmpeg directly to verify written files by demuxing and decoding
# them back.
target_link_libraries(voxmesh-media-container-tests PRIVATE
    voxmesh::media-container
    voxmesh::audio-core
    voxmesh::build_flags
    GTest::gtest_main
    ffmpeg::avformat
    ffmpeg::avcodec
    ffmpeg::avutil)

gtest_discover_tests(voxmesh-media-container-tests)
