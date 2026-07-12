# Performance benchmark harness (master prompt §31 Phase 1). Built by default to
# prevent bit-rot; run manually:  ./voxmesh-audio-core-bench --benchmark_min_time=1s
find_package(benchmark REQUIRED)

add_executable(voxmesh-audio-core-bench
    ring_buffer_bench.cpp)

target_link_libraries(voxmesh-audio-core-bench PRIVATE
    voxmesh::audio-core
    voxmesh::build_flags
    benchmark::benchmark)
