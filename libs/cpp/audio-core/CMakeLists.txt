find_package(nlohmann_json REQUIRED)

add_library(voxmesh-audio-core STATIC
    src/clock_sync.cpp
    src/config.cpp
    src/session.cpp
    src/testing/fake_backend.cpp
    src/testing/signal_generator.cpp)
add_library(voxmesh::audio-core ALIAS voxmesh-audio-core)

target_include_directories(voxmesh-audio-core PUBLIC include)
# JSON stays an implementation detail of config.cpp — no json types in public headers.
target_link_libraries(voxmesh-audio-core
    PRIVATE
        voxmesh::build_flags
        nlohmann_json::nlohmann_json)

if(VOXMESH_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(VOXMESH_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
