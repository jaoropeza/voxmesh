#include "voxmesh/dsp/fir_decimator.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace voxmesh::dsp {

namespace {

// Windowed-sinc low-pass at `cutoff` (normalized to input Nyquist, 0..1),
// Hamming window, unity DC gain.
std::vector<double> designLowPass(std::size_t taps, double cutoff)
{
    std::vector<double> h(taps);
    const auto center = static_cast<double>(taps - 1) / 2.0;
    double sum = 0.0;
    for (std::size_t i = 0; i < taps; ++i) {
        const double n = static_cast<double>(i) - center;
        const double x = std::numbers::pi * cutoff * n;
        const double sinc = n == 0.0 ? 1.0 : std::sin(x) / x;
        const double window =
            0.54 - 0.46 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(taps - 1));
        h[i] = cutoff * sinc * window;
        sum += h[i];
    }
    for (double& c : h) {
        c /= sum; // normalize to exactly unity DC gain
    }
    return h;
}

std::int16_t clampToS16(double value)
{
    const double rounded = std::lround(value);
    return static_cast<std::int16_t>(std::clamp(rounded, -32768.0, 32767.0));
}

} // namespace

FirDecimator::FirDecimator(std::size_t factor, std::size_t taps)
    : factor_(factor == 0 ? 1 : factor),
      coefficients_(factor_ == 1 ? std::vector<double>{1.0}
                                 : designLowPass(taps % 2 == 0 ? taps + 1 : taps, 0.8 / static_cast<double>(factor_))),
      history_(coefficients_.size(), 0.0)
{
}

void FirDecimator::process(std::span<const std::int16_t> input, std::vector<std::int16_t>& output)
{
    const std::size_t taps = coefficients_.size();
    for (const std::int16_t sample : input) {
        history_[historyPos_] = static_cast<double>(sample);
        if (phase_ == 0) {
            // Convolve: history_[historyPos_] is the newest sample x[n];
            // coefficient k applies to x[n-k].
            double acc = 0.0;
            std::size_t index = historyPos_;
            for (std::size_t k = 0; k < taps; ++k) {
                acc += coefficients_[k] * history_[index];
                index = index == 0 ? taps - 1 : index - 1;
            }
            output.push_back(clampToS16(acc));
        }
        phase_ = (phase_ + 1) % factor_;
        historyPos_ = (historyPos_ + 1) % taps;
    }
}

void FirDecimator::reset()
{
    std::fill(history_.begin(), history_.end(), 0.0);
    historyPos_ = 0;
    phase_ = 0;
}

} // namespace voxmesh::dsp
