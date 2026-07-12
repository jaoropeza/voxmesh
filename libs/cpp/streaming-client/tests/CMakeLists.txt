add_executable(voxmesh-streaming-client-tests
    grpc_stream_test.cpp)

target_link_libraries(voxmesh-streaming-client-tests PRIVATE
    voxmesh::streaming-client
    voxmesh::audio-core
    voxmesh::build_flags
    GTest::gtest_main)

gtest_discover_tests(voxmesh-streaming-client-tests)
