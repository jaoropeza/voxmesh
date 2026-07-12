#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace voxmesh::dsp {

// Integer-factor sample-rate reduction for mono pcm_s16le with a windowed-sinc
// anti-aliasing low-pass (Hamming). Used to derive the 16 kHz STT stream from
// 48 kHz archival audio (master prompt §7): factor 3.
//
// Deterministic and allocation-free in process() except for output growth; not
// for the real-time capture path (§8) — it runs on the drain thread.
// Group delay is (taps-1)/2 input samples (~0.65 ms at 48 kHz with 63 taps);
// output timestamps ignore it.
class FirDecimator {
public:
    // factor must be >= 1; taps must be odd. Cutoff is placed at 80% of the
    // output Nyquist for aliasing headroom.
    FirDecimator(std::size_t factor, std::size_t taps = 63);

    // Consumes any number of samples, appends decimated samples to `output`.
    // State carries across calls, so consecutive calls process one continuous
    // signal.
    void process(std::span<const std::int16_t> input, std::vector<std::int16_t>& output);

    // Drops filter state (history and phase); call across a discontinuity so
    // unrelated audio does not smear through the filter.
    void reset();

    [[nodiscard]] std::size_t factor() const { return factor_; }

private:
    std::size_t factor_;
    std::vector<double> coefficients_;
    // Circular history of the most recent (taps) input samples.
    std::vector<double> history_;
    std::size_t historyPos_{0};
    // Input samples until the next output sample is due (decimation phase).
    std::size_t phase_{0};
};

} // namespace voxmesh::dsp
