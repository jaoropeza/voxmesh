add_executable(voxmesh-audio-core-tests
    clock_sync_test.cpp
    config_test.cpp
    fake_backend_test.cpp
    ring_buffer_test.cpp
    session_test.cpp
    signal_generator_test.cpp)

target_link_libraries(voxmesh-audio-core-tests PRIVATE
    voxmesh::audio-core
    voxmesh::build_flags
    GTest::gtest_main)

gtest_discover_tests(voxmesh-audio-core-tests)
