add_executable(voxmesh-audio-dsp-tests
    fir_decimator_test.cpp
    stt_stream_producer_test.cpp)

target_link_libraries(voxmesh-audio-dsp-tests PRIVATE
    voxmesh::audio-dsp
    voxmesh::audio-core
    voxmesh::build_flags
    GTest::gtest_main)

gtest_discover_tests(voxmesh-audio-dsp-tests)
