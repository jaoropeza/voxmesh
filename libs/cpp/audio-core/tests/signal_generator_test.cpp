#include "voxmesh/audio/testing/signal_generator.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace voxmesh::audio::testing {
namespace {

TEST(SignalGeneratorTest, SilenceIsAllZeros)
{
    SignalGenerator generator({.waveform = Waveform::Silence});
    std::vector<std::int16_t> samples(480, std::int16_t{1234});
    generator.generate(samples);
    EXPECT_TRUE(std::all_of(samples.begin(), samples.end(), [](std::int16_t s) { return s == 0; }));
}

TEST(SignalGeneratorTest, RampIsDeterministicAcrossInstances)
{
    const GeneratorConfig config{.waveform = Waveform::Ramp, .sampleRate = SampleRate{48000}, .frequencyHz = 100.0};
    SignalGenerator first(config);
    SignalGenerator second(config);

    std::vector<std::int16_t> a(960);
    std::vector<std::int16_t> b(960);
    first.generate(a);
    second.generate(b);
    EXPECT_EQ(a, b);
}

TEST(SignalGeneratorTest, PhaseContinuesAcrossCalls)
{
    const GeneratorConfig config{.waveform = Waveform::Ramp, .sampleRate = SampleRate{48000}, .frequencyHz = 100.0};
    SignalGenerator oneShot(config);
    SignalGenerator chunked(config);

    std::vector<std::int16_t> whole(960);
    oneShot.generate(whole);

    std::vector<std::int16_t> firstHalf(480);
    std::vector<std::int16_t> secondHalf(480);
    chunked.generate(firstHalf);
    chunked.generate(secondHalf);

    std::vector<std::int16_t> stitched;
    stitched.insert(stitched.end(), firstHalf.begin(), firstHalf.end());
    stitched.insert(stitched.end(), secondHalf.begin(), secondHalf.end());
    EXPECT_EQ(whole, stitched);
}

TEST(SignalGeneratorTest, RampSpansConfiguredAmplitude)
{
    // 480 samples per period at 100 Hz / 48 kHz.
    SignalGenerator generator(
        {.waveform = Waveform::Ramp, .sampleRate = SampleRate{48000}, .frequencyHz = 100.0, .amplitude = 0.5});
    std::vector<std::int16_t> samples(480);
    generator.generate(samples);

    EXPECT_EQ(samples.front(), static_cast<std::int16_t>(-16384)); // llround(-0.5 * 32767)
    EXPECT_EQ(samples.back(), static_cast<std::int16_t>(16384));   // llround(+0.5 * 32767)
    EXPECT_TRUE(std::is_sorted(samples.begin(), samples.end()));
}

TEST(SignalGeneratorTest, InterleavedChannelsCarryTheSameSample)
{
    SignalGenerator generator({.waveform = Waveform::Ramp,
                               .sampleRate = SampleRate{48000},
                               .channels = ChannelCount{2},
                               .frequencyHz = 100.0});
    std::vector<std::int16_t> samples(960); // 480 frames x 2 channels
    generator.generate(samples);
    for (std::size_t frame = 0; frame < 480; ++frame) {
        ASSERT_EQ(samples[frame * 2], samples[frame * 2 + 1]) << frame;
    }
}

TEST(SignalGeneratorTest, SineStaysWithinAmplitudeAndOscillates)
{
    SignalGenerator generator(
        {.waveform = Waveform::Sine, .sampleRate = SampleRate{16000}, .frequencyHz = 440.0, .amplitude = 0.25});
    std::vector<std::int16_t> samples(16000);
    generator.generate(samples);

    const auto bound = static_cast<std::int16_t>(0.25 * 32767.0 + 1.0);
    std::int16_t minSeen = 0;
    std::int16_t maxSeen = 0;
    for (const auto s : samples) {
        EXPECT_LE(std::abs(int{s}), int{bound});
        minSeen = std::min(minSeen, s);
        maxSeen = std::max(maxSeen, s);
    }
    // One second of a 440 Hz tone reaches close to both peaks.
    EXPECT_GT(int{maxSeen}, int{bound} * 9 / 10);
    EXPECT_LT(int{minSeen}, -int{bound} * 9 / 10);
}

} // namespace
} // namespace voxmesh::audio::testing
