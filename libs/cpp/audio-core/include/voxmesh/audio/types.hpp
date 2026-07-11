#pragma once

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>

namespace voxmesh::audio {

// Strong types for the audio domain (master prompt §8): sample rates, timestamps,
// sequence numbers, and durations never travel as bare integers.

enum class SampleFormat : std::uint8_t {
    PcmS16Le,
    Float32Le,
};

[[nodiscard]] constexpr std::size_t bytesPerSample(SampleFormat format)
{
    switch (format) {
    case SampleFormat::PcmS16Le:
        return 2;
    case SampleFormat::Float32Le:
        return 4;
    }
    return 0;
}

// The synchronized tracks of a recording (master prompt §7, ADR-0006).
enum class TrackKind : std::uint8_t {
    Microphone,
    SystemOutput,
    EchoCancelledMicrophone,
    Mix,
    SttStream,
};

struct SampleRate {
    std::uint32_t hz{0};

    [[nodiscard]] constexpr auto operator<=>(const SampleRate&) const = default;
};

struct ChannelCount {
    std::uint16_t value{0};

    [[nodiscard]] constexpr auto operator<=>(const ChannelCount&) const = default;
};

// Monotonically increasing per track; gaps signal loss and must be paired with a
// discontinuity flag on the next frame (ADR-0007).
struct SequenceNumber {
    std::uint64_t value{0};

    [[nodiscard]] constexpr SequenceNumber next() const { return SequenceNumber{value + 1}; }
    [[nodiscard]] constexpr auto operator<=>(const SequenceNumber&) const = default;
};

// Monotonic-clock timestamp; the synchronization authority for cross-track
// alignment (ADR-0007). Not related to wall-clock time — deterministic tests and
// fake backends may drive it manually.
struct MonotonicTimestamp {
    std::chrono::nanoseconds value{0};

    [[nodiscard]] constexpr MonotonicTimestamp advancedBy(std::chrono::nanoseconds delta) const
    {
        return MonotonicTimestamp{value + delta};
    }
    [[nodiscard]] constexpr auto operator<=>(const MonotonicTimestamp&) const = default;
};

// Samples per channel in a frame of the given duration. Rates and durations in
// this codebase are chosen so this divides evenly (e.g. 48000 Hz / 10 ms = 480).
[[nodiscard]] constexpr std::size_t samplesPerChannel(SampleRate rate, std::chrono::milliseconds frameDuration)
{
    return static_cast<std::size_t>(rate.hz) * static_cast<std::size_t>(frameDuration.count()) / std::size_t{1000};
}

} // namespace voxmesh::audio
