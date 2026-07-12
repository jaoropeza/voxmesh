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

    # FFmpeg trimmed to the recording writer's needs (issue #13): FLAC encode/decode
    # and FLAC/Matroska (de)muxing over file I/O, nothing else. Keeping every
    # external codec and GPL component off keeps the build LGPL-2.1 and fast.
    default_options = {
        "ffmpeg/*:avdevice": False,
        "ffmpeg/*:avfilter": False,
        "ffmpeg/*:swscale": False,
        "ffmpeg/*:postproc": False,
        "ffmpeg/*:swresample": True,
        "ffmpeg/*:with_programs": False,
        "ffmpeg/*:disable_everything": True,
        "ffmpeg/*:enable_encoders": "flac",
        "ffmpeg/*:enable_decoders": "flac",
        "ffmpeg/*:enable_muxers": "flac,matroska",
        "ffmpeg/*:enable_demuxers": "flac,matroska",
        "ffmpeg/*:enable_parsers": "flac",
        "ffmpeg/*:enable_protocols": "file",
        "ffmpeg/*:with_bzip2": False,
        "ffmpeg/*:with_freetype": False,
        "ffmpeg/*:with_libaom": False,
        "ffmpeg/*:with_libdav1d": False,
        "ffmpeg/*:with_libfdk_aac": False,
        "ffmpeg/*:with_libiconv": False,
        "ffmpeg/*:with_libmp3lame": False,
        "ffmpeg/*:with_libsvtav1": False,
        "ffmpeg/*:with_libvpx": False,
        "ffmpeg/*:with_libwebp": False,
        "ffmpeg/*:with_libx264": False,
        "ffmpeg/*:with_libx265": False,
        "ffmpeg/*:with_lzma": False,
        "ffmpeg/*:with_openh264": False,
        "ffmpeg/*:with_openjpeg": False,
        "ffmpeg/*:with_opus": False,
        "ffmpeg/*:with_ssl": False,
        "ffmpeg/*:with_vorbis": False,
        "ffmpeg/*:with_zlib": True,
        # Platform-specific device/hwaccel integrations conflict with
        # avdevice=False (e.g. "with_libalsa requires avdevice"); pattern-scoped
        # options that don't exist on a platform are ignored.
        "ffmpeg/*:with_libalsa": False,
        "ffmpeg/*:with_pulse": False,
        "ffmpeg/*:with_vaapi": False,
        "ffmpeg/*:with_vdpau": False,
        "ffmpeg/*:with_vulkan": False,
        "ffmpeg/*:with_xcb": False,
        "ffmpeg/*:with_xlib": False,
        "ffmpeg/*:with_appkit": False,
        "ffmpeg/*:with_avfoundation": False,
        "ffmpeg/*:with_coreimage": False,
        "ffmpeg/*:with_audiotoolbox": False,
        "ffmpeg/*:with_videotoolbox": False,
    }

    def requirements(self):
        self.requires("gtest/1.15.0")
        self.requires("nlohmann_json/3.11.3")
        self.requires("benchmark/1.9.1")
        self.requires("ffmpeg/7.1.5")
        # Later phases (pin when introduced): protobuf, grpc,
        # webrtc-audio-processing, opentelemetry-cpp.
