add_executable(voxmesh-audio-wasapi-tests
    wasapi_backend_test.cpp)

target_link_libraries(voxmesh-audio-wasapi-tests PRIVATE
    voxmesh::audio-wasapi
    voxmesh::build_flags
    GTest::gtest_main)

gtest_discover_tests(voxmesh-audio-wasapi-tests)
