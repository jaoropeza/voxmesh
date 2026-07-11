#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace voxmesh::audio {

struct RingBufferStats {
    std::uint64_t pushed{0};
    std::uint64_t popped{0};
    std::uint64_t rejected{0};
};

// Bounded lock-free single-producer/single-consumer ring buffer — the only queue
// type allowed on the capture path (master prompt §8). Capacity is fixed at
// construction; a full buffer rejects the push (counted, never blocking, never
// growing). Exactly one producer thread may call tryPush and exactly one consumer
// thread may call tryPop.
template <typename T> class SpscRingBuffer {
public:
    // Allocates capacity + 1 slots up front; no allocation happens afterwards.
    explicit SpscRingBuffer(std::size_t capacity) : slots_(capacity + 1) {}

    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;
    SpscRingBuffer(SpscRingBuffer&&) = delete;
    SpscRingBuffer& operator=(SpscRingBuffer&&) = delete;
    ~SpscRingBuffer() = default;

    // Producer side. Returns false (and counts the rejection) when full; the item
    // is left unmoved so the caller can account for the loss.
    [[nodiscard]] bool tryPush(T&& item)
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = increment(head);
        if (next == tail_.load(std::memory_order_acquire)) {
            rejected_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        slots_[head] = std::move(item);
        head_.store(next, std::memory_order_release);
        pushed_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    // Consumer side. Empty buffer yields std::nullopt.
    [[nodiscard]] std::optional<T> tryPop()
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return std::nullopt;
        }
        std::optional<T> item{std::move(slots_[tail])};
        tail_.store(increment(tail), std::memory_order_release);
        popped_.fetch_add(1, std::memory_order_relaxed);
        return item;
    }

    [[nodiscard]] std::size_t capacity() const { return slots_.size() - 1; }

    // Approximate when called concurrently; exact when the buffer is quiescent.
    [[nodiscard]] std::size_t size() const
    {
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        return head >= tail ? head - tail : slots_.size() - tail + head;
    }

    [[nodiscard]] bool empty() const { return size() == 0; }

    [[nodiscard]] RingBufferStats stats() const
    {
        return RingBufferStats{
            pushed_.load(std::memory_order_relaxed),
            popped_.load(std::memory_order_relaxed),
            rejected_.load(std::memory_order_relaxed),
        };
    }

private:
    [[nodiscard]] std::size_t increment(std::size_t index) const { return index + 1 == slots_.size() ? 0 : index + 1; }

    std::vector<T> slots_;
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
    std::atomic<std::uint64_t> pushed_{0};
    std::atomic<std::uint64_t> popped_{0};
    std::atomic<std::uint64_t> rejected_{0};
};

} // namespace voxmesh::audio
