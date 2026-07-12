#pragma once

#include "voxmesh/audio/recording.hpp"

#include <memory>
#include <variant>

namespace voxmesh::media {

using RecordingWriterResult = std::variant<std::unique_ptr<audio::IRecordingWriter>, audio::RecordingWriterError>;

// Creates an FFmpeg-backed lossless recording writer (issue #13). Container
// follows the track count: a single track becomes a raw FLAC stream, several
// tracks become a Matroska file — the caller picks the matching extension
// (.flac / .mka). Tracks encode as FLAC, so formats must be integer PCM
// (master prompt §7 allows pcm_s16le archival).
[[nodiscard]] RecordingWriterResult createRecordingWriter(const audio::RecordingWriterConfig& config);

} // namespace voxmesh::media
