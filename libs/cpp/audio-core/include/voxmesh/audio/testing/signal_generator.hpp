#pragma once

#include "voxmesh/audio/types.hpp"

#include <cstdint>
#include <span>

namespace voxmesh::audio::testing {

// Deterministic PCM signal source for component tests and fixtures (master
// prompt §31 Phase 1). Given the same configuration and call sequence, output is
// byte-identical — Ramp and Silence exactly on every platform; Sine uses
// std::sin, so tests assert on it with tolerance rather than exact samples.
enum class Waveform : std::uint8_t {
    Silence,
    // Integer sawtooth from -amplitude to +amplitude over each period;
    // exactly reproducible across platforms.
    Ramp,
    Sine,
};

struct GeneratorConfig {
    Waveform waveform{Waveform::Ramp};
    SampleRate sampleRate{48000};
    ChannelCount channels{1};
    double frequencyHz{440.0};
    // Peak level as a fraction of int16 full scale, in [0.0, 1.0].
    double amplitude{0.5};
};

class SignalGenerator {
public:
    explicit SignalGenerator(GeneratorConfig config);

    // Fills the span with interleaved samples; the span size must be a multiple
    // of the channel count. The phase continues across calls, so consecutive
    // calls produce one continuous signal.
    void generate(std::span<std::int16_t> interleaved);

    [[nodiscard]] const GeneratorConfig& config() const { return config_; }

private:
    [[nodiscard]] std::int16_t sampleAt(std::uint64_t position) const;

    GeneratorConfig config_;
    std::uint64_t position_{0};
    std::uint64_t samplesPerPeriod_{0};
};

} // namespace voxmesh::audio::testing
