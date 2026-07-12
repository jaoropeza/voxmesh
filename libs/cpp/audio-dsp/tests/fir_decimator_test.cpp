#include "voxmesh/dsp/fir_decimator.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace {

using voxmesh::dsp::FirDecimator;

std::vector<std::int16_t> sine(double frequencyHz, double sampleRateHz, std::size_t count, double amplitude)
{
    std::vector<std::int16_t> samples(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double phase = 2.0 * std::numbers::pi * frequencyHz * static_cast<double>(i) / sampleRateHz;
        samples[i] = static_cast<std::int16_t>(amplitude * std::sin(phase));
    }
    return samples;
}

double rms(const std::vector<std::int16_t>& samples, std::size_t skip)
{
    double acc = 0.0;
    std::size_t counted = 0;
    for (std::size_t i = skip; i < samples.size(); ++i) {
        acc += static_cast<double>(samples[i]) * static_cast<double>(samples[i]);
        ++counted;
    }
    return counted == 0 ? 0.0 : std::sqrt(acc / static_cast<double>(counted));
}

TEST(FirDecimatorTest, OutputCountIsInputOverFactor)
{
    FirDecimator decimator(3);
    std::vector<std::int16_t> output;
    decimator.process(std::vector<std::int16_t>(4800, 0), output);
    EXPECT_EQ(output.size(), 1600u);
}

TEST(FirDecimatorTest, StateCarriesAcrossCalls)
{
    FirDecimator decimator(3);
    std::vector<std::int16_t> output;
    // 480-sample chunks (10 ms at 48 kHz) must chain into a seamless stream.
    for (int i = 0; i < 10; ++i) {
        decimator.process(std::vector<std::int16_t>(480, 1000), output);
    }
    EXPECT_EQ(output.size(), 1600u);
}

TEST(FirDecimatorTest, DcPassesAtUnityGain)
{
    FirDecimator decimator(3);
    std::vector<std::int16_t> output;
    decimator.process(std::vector<std::int16_t>(4800, 10000), output);
    // After the filter settles, DC must come through at unity gain.
    EXPECT_NEAR(static_cast<double>(output.back()), 10000.0, 1.0);
}

TEST(FirDecimatorTest, PassbandToneSurvives)
{
    FirDecimator decimator(3);
    std::vector<std::int16_t> output;
    decimator.process(sine(1000.0, 48000.0, 48000, 10000.0), output);
    // 1 kHz is deep in the passband of the 16 kHz output; RMS of a sine is
    // amplitude/sqrt(2) ~= 7071. Allow a little ripple.
    EXPECT_NEAR(rms(output, 100), 7071.0, 250.0);
}

TEST(FirDecimatorTest, AliasingToneIsSuppressed)
{
    FirDecimator decimator(3);
    std::vector<std::int16_t> output;
    // 10 kHz is above the 8 kHz output Nyquist: without the anti-alias filter
    // it would fold to 6 kHz at full amplitude.
    decimator.process(sine(10000.0, 48000.0, 48000, 10000.0), output);
    const double attenuationDb = 20.0 * std::log10(rms(output, 100) / 7071.0);
    EXPECT_LT(attenuationDb, -40.0);
}

TEST(FirDecimatorTest, FactorOneIsPassthrough)
{
    FirDecimator decimator(1);
    std::vector<std::int16_t> output;
    const auto input = sine(1000.0, 16000.0, 1600, 12000.0);
    decimator.process(input, output);
    EXPECT_EQ(output, input);
}

TEST(FirDecimatorTest, ResetClearsHistoryAndPhase)
{
    FirDecimator decimator(3);
    std::vector<std::int16_t> first;
    decimator.process(sine(1000.0, 48000.0, 4800, 10000.0), first);

    decimator.reset();
    std::vector<std::int16_t> second;
    decimator.process(sine(1000.0, 48000.0, 4800, 10000.0), second);
    EXPECT_EQ(first, second);
}

} // namespace
