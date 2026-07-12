#pragma once

#include "voxmesh/audio/config.hpp"
#include "voxmesh/audio/frame.hpp"
#include "voxmesh/audio/types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace voxmesh::audio {

// Recording writer port (master prompt §7, §11; issue #13). Implementations live
// outside audio-core (e.g. libs/cpp/media-io wraps FFmpeg); this header keeps the
// consumer side codec- and container-agnostic.

enum class RecordingWriterError : std::uint8_t {
    // Track list empty, unsupported sample format (lossless archival encodes
    // integer PCM only), or invalid rate/channel count.
    InvalidConfig,
    // Output directory missing or partial file could not be created.
    OpenFailed,
    // A frame did not match its track's configured format.
    FormatMismatch,
    // Encoding or container write failed mid-recording.
    WriteFailed,
    // Draining, trailer write, or the atomic rename onto the final path failed.
    FinalizeFailed,
    // Operation not legal in the writer's current state (e.g. write after
    // finalize).
    InvalidState,
};

[[nodiscard]] constexpr std::string_view toString(RecordingWriterError error)
{
    switch (error) {
    case RecordingWriterError::InvalidConfig:
        return "InvalidConfig";
    case RecordingWriterError::OpenFailed:
        return "OpenFailed";
    case RecordingWriterError::FormatMismatch:
        return "FormatMismatch";
    case RecordingWriterError::WriteFailed:
        return "WriteFailed";
    case RecordingWriterError::FinalizeFailed:
        return "FinalizeFailed";
    case RecordingWriterError::InvalidState:
        return "InvalidState";
    }
    return "Unknown";
}

struct RecordingTrackConfig {
    TrackKind kind{TrackKind::Microphone};
    TrackFormat format{};
    // Container-visible track title ("Microphone", "System output"); never user
    // or meeting content (ADR-0019).
    std::string title{};
};

struct RecordingWriterConfig {
    // Final path including extension; the writer produces it atomically (§11):
    // all output goes to a sibling partial file that is renamed onto this path
    // only by a successful finalize().
    std::filesystem::path outputFile{};
    std::vector<RecordingTrackConfig> tracks{};
};

struct RecordingWriterStats {
    // AudioFrames accepted by write() across all tracks.
    std::uint64_t framesWritten{0};
    // Per-channel samples written, indexed like RecordingWriterConfig::tracks.
    std::vector<std::uint64_t> samplesWrittenPerTrack{};
};

// Writes synchronized tracks to a lossless local recording. Not thread-safe:
// callers serialize write/finalize/abort externally. Never call from a
// real-time capture callback — this is the file-I/O side of the pipeline (§8).
//
// Timestamp policy: frames are appended in arrival order and stamped by
// accumulated written samples per track. Gap healing happens upstream in the
// clock synchronizer (ADR-0007); a session pause is therefore spliced out of
// the file rather than recorded as silence.
class IRecordingWriter {
public:
    IRecordingWriter() = default;
    IRecordingWriter(const IRecordingWriter&) = delete;
    IRecordingWriter& operator=(const IRecordingWriter&) = delete;
    virtual ~IRecordingWriter() = default;

    // trackIndex addresses RecordingWriterConfig::tracks. Returns false on
    // failure; lastError() then explains it and the writer refuses further
    // writes until abort().
    [[nodiscard]] virtual bool write(std::size_t trackIndex, const AudioFrame& frame) = 0;

    // Drains encoders, writes the container trailer, and atomically renames the
    // partial file onto outputFile. On failure the partial file is left in
    // place for crash-recovery inspection (§11).
    [[nodiscard]] virtual bool finalize() = 0;

    // Deliberately discards the recording: closes everything and removes the
    // partial file. Safe to call in any state; the writer becomes inert. This
    // is the only path that deletes data — destroying a writer without
    // finalize() merely closes handles and leaves the partial file for
    // recovery (§11).
    virtual void abort() noexcept = 0;

    // Meaningful after write()/finalize() returned false.
    [[nodiscard]] virtual RecordingWriterError lastError() const = 0;

    [[nodiscard]] virtual const RecordingWriterStats& stats() const = 0;
};

} // namespace voxmesh::audio
