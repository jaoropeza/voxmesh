from conan import ConanFile


class VoxMeshConan(ConanFile):
    """Third-party C++ dependencies for VoxMesh (ADR-0005).

    All versions are pinned exactly. Add a row to LICENSES/README.md for every
    new dependency. Usage:

        conan install . --output-folder build/conan --build=missing -s build_type=Debug
    """

    name = "voxmesh"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeToolchain", "CMakeDeps"

    def requirements(self):
        self.requires("gtest/1.15.0")
        self.requires("nlohmann_json/3.11.3")
        self.requires("benchmark/1.9.1")
        # Later phases (pin when introduced): protobuf, grpc, ffmpeg,
        # webrtc-audio-processing, opentelemetry-cpp.
