#include "voxmesh/audio/frame.hpp"
#include "voxmesh/audio/ring_buffer.hpp"

#include <benchmark/benchmark.h>

#include <cstddef>
#include <utility>

namespace {

using voxmesh::audio::AudioFrame;
using voxmesh::audio::SpscRingBuffer;

// Baseline cost of one push+pop cycle with a trivial payload.
void BM_RingBufferPushPopUint64(benchmark::State& state)
{
    SpscRingBuffer<std::uint64_t> buffer(1024);
    std::uint64_t value = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(buffer.tryPush(std::uint64_t{value++}));
        auto item = buffer.tryPop();
        benchmark::DoNotOptimize(item);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()));
}
BENCHMARK(BM_RingBufferPushPopUint64);

// Push+pop of a realistic capture frame (48 kHz, 10 ms, mono s16 = 960 bytes).
// The payload is recycled between iterations, mirroring the §8 rule that the
// steady state allocates nothing.
void BM_RingBufferPushPopAudioFrame(benchmark::State& state)
{
    SpscRingBuffer<AudioFrame> buffer(256);
    AudioFrame frame;
    frame.payload.resize(960);
    for (auto _ : state) {
        benchmark::DoNotOptimize(buffer.tryPush(std::move(frame)));
        auto popped = buffer.tryPop();
        benchmark::DoNotOptimize(popped);
        frame = std::move(*popped); // recycle the allocation
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()));
}
BENCHMARK(BM_RingBufferPushPopAudioFrame);

// Burst behavior: fill the ring, then drain it.
void BM_RingBufferFillDrain(benchmark::State& state)
{
    const auto capacity = static_cast<std::size_t>(state.range(0));
    SpscRingBuffer<std::uint64_t> buffer(capacity);
    for (auto _ : state) {
        for (std::uint64_t i = 0; i < capacity; ++i) {
            benchmark::DoNotOptimize(buffer.tryPush(std::uint64_t{i}));
        }
        while (auto item = buffer.tryPop()) {
            benchmark::DoNotOptimize(item);
        }
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(capacity));
}
BENCHMARK(BM_RingBufferFillDrain)->Arg(64)->Arg(512)->Arg(4096);

} // namespace

BENCHMARK_MAIN();
