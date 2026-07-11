#include "voxmesh/audio/testing/signal_generator.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace voxmesh::audio::testing {

namespace {

constexpr double kInt16FullScale = 32767.0;

[[nodiscard]] std::int16_t clampToInt16(double value)
{
    const double clamped = std::clamp(value, -32768.0, 32767.0);
    return static_cast<std::int16_t>(std::llround(clamped));
}

} // namespace

SignalGenerator::SignalGenerator(GeneratorConfig config) : config_(config)
{
    if (config_.frequencyHz > 0.0) {
        const double period = static_cast<double>(config_.sampleRate.hz) / config_.frequencyHz;
        samplesPerPeriod_ = std::max<std::uint64_t>(1, static_cast<std::uint64_t>(std::llround(period)));
    } else {
        samplesPerPeriod_ = 1;
    }
}

std::int16_t SignalGenerator::sampleAt(std::uint64_t position) const
{
    switch (config_.waveform) {
    case Waveform::Silence:
        return 0;
    case Waveform::Ramp: {
        const std::uint64_t phase = position % samplesPerPeriod_;
        // Rises linearly from -1.0 to +1.0 over one period; integer phase math
        // keeps the result exactly reproducible.
        const double normalized = samplesPerPeriod_ == 1 ? 0.0
                                                         : -1.0 + 2.0 * static_cast<double>(phase) /
                                                                      static_cast<double>(samplesPerPeriod_ - 1);
        return clampToInt16(normalized * config_.amplitude * kInt16FullScale);
    }
    case Waveform::Sine: {
        const double t = static_cast<double>(position) / static_cast<double>(config_.sampleRate.hz);
        const double value = std::sin(2.0 * std::numbers::pi * config_.frequencyHz * t);
        return clampToInt16(value * config_.amplitude * kInt16FullScale);
    }
    }
    return 0;
}

void SignalGenerator::generate(std::span<std::int16_t> interleaved)
{
    const std::size_t channels = std::max<std::size_t>(1, config_.channels.value);
    const std::size_t frames = interleaved.size() / channels;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const std::int16_t value = sampleAt(position_ + frame);
        for (std::size_t channel = 0; channel < channels; ++channel) {
            interleaved[frame * channels + channel] = value;
        }
    }
    position_ += frames;
}

} // namespace voxmesh::audio::testing
