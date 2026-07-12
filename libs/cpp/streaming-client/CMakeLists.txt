# Streaming STT transport (issue #15, ADR-0008): gRPC client behind the
# ISttStreamClient port, plus the Phase 2 mock server test double. gRPC and
# protobuf stay implementation details — no generated or grpc types in public
# headers.
find_package(Protobuf REQUIRED CONFIG)
find_package(gRPC REQUIRED CONFIG)

# Generated protobuf/grpc sources live in their own target so the project's
# strict warning flags never apply to generated code; SYSTEM include keeps
# consumer builds quiet about generated headers too.
add_library(voxmesh-stt-proto STATIC
    ${CMAKE_SOURCE_DIR}/libs/contracts/proto/voxmesh/media/v1/audio_frame.proto
    ${CMAKE_SOURCE_DIR}/libs/contracts/proto/voxmesh/transcript/v1/transcript_segment.proto
    ${CMAKE_SOURCE_DIR}/libs/contracts/proto/voxmesh/stt/v1/stt_stream_service.proto)
target_include_directories(voxmesh-stt-proto SYSTEM PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(voxmesh-stt-proto PUBLIC protobuf::libprotobuf gRPC::grpc++)

protobuf_generate(TARGET voxmesh-stt-proto
    IMPORT_DIRS ${CMAKE_SOURCE_DIR}/libs/contracts/proto
    PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
protobuf_generate(TARGET voxmesh-stt-proto
    LANGUAGE grpc
    GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
    PLUGIN "protoc-gen-grpc=\$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
    IMPORT_DIRS ${CMAKE_SOURCE_DIR}/libs/contracts/proto
    PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)

add_library(voxmesh-streaming-client STATIC
    src/grpc_stt_stream_client.cpp
    src/testing/mock_stt_server.cpp)
add_library(voxmesh::streaming-client ALIAS voxmesh-streaming-client)

target_include_directories(voxmesh-streaming-client PUBLIC include)
target_link_libraries(voxmesh-streaming-client
    PUBLIC
        voxmesh::audio-core
    PRIVATE
        voxmesh-stt-proto
        voxmesh::build_flags)

if(VOXMESH_BUILD_TESTS)
    add_subdirectory(tests)
endif()
