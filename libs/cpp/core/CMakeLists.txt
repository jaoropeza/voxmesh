add_library(voxmesh-core STATIC
    src/version.cpp)
add_library(voxmesh::core ALIAS voxmesh-core)

target_include_directories(voxmesh-core PUBLIC include)
target_compile_definitions(voxmesh-core PRIVATE VOXMESH_VERSION="${PROJECT_VERSION}")
target_link_libraries(voxmesh-core PRIVATE voxmesh::build_flags)

if(VOXMESH_BUILD_TESTS)
    add_subdirectory(tests)
endif()
