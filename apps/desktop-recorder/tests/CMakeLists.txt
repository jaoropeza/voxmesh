add_executable(voxmesh-recorder-tests
    recorder_controller_test.cpp)

target_link_libraries(voxmesh-recorder-tests PRIVATE
    voxmesh-recorder-core
    voxmesh::build_flags
    GTest::gtest) # custom main: tests need a QCoreApplication

# Qt is linked dynamically; on Windows the DLL must sit next to the binary for
# gtest discovery (and ctest) to be able to run it.
if(WIN32)
    add_custom_command(TARGET voxmesh-recorder-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Qt6::Core>
            $<TARGET_FILE_DIR:voxmesh-recorder-tests>)
endif()

gtest_discover_tests(voxmesh-recorder-tests)
