add_executable(voxmesh-core-tests
    version_test.cpp)

target_link_libraries(voxmesh-core-tests PRIVATE
    voxmesh::core
    voxmesh::build_flags
    GTest::gtest_main)

gtest_discover_tests(voxmesh-core-tests)
