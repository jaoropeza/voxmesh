#pragma once

#include "voxmesh/audio/types.hpp"

#include <cstddef>
#include <vector>

namespace voxmesh::audio {

// One captured audio frame travelling from a capture backend towards encoding,
// persistence, and streaming. In-memory counterpart of voxmesh.media.v1.AudioFrame.
//
// Real-time note (master prompt §8): producers preallocate the payload; passing a
// frame through the pipeline moves it and must not allocate. Copying is possible
// but reserved for non-real-time paths.
struct AudioFrame {
    TrackKind track{TrackKind::Microphone};
    SequenceNumber sequence{};
    MonotonicTimestamp timestamp{};
    SampleRate sampleRate{};
    ChannelCount channels{};
    SampleFormat format{SampleFormat::PcmS16Le};
    // True when this frame does not butt seamlessly against the previous sequence
    // number on its track (loss, device restart, inserted silence). Consumers must
    // not interpolate across it (ADR-0007).
    bool discontinuity{false};
    std::vector<std::byte> payload{};

    [[nodiscard]] std::size_t sampleCountPerChannel() const
    {
        const std::size_t stride = bytesPerSample(format) * static_cast<std::size_t>(channels.value);
        return stride == 0 ? 0 : payload.size() / stride;
    }
};

} // namespace voxmesh::audio
