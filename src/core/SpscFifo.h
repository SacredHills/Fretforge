#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace ff
{
/** Single-producer / single-consumer lock-free FIFO of trivially copyable items.
    Storage is allocated once in prepare(); push/pop never allocate or lock, so the
    producer side is safe to call from the real-time audio thread. */
template <typename T>
class SpscFifo
{
public:
    void prepare (int capacity)
    {
        storage.assign ((size_t) capacity, T{});
        fifo.setTotalSize (capacity);
        fifo.reset();
    }

    int capacity() const noexcept { return fifo.getTotalSize(); }
    int numReady() const noexcept { return fifo.getNumReady(); }
    int freeSpace() const noexcept { return fifo.getFreeSpace(); }

    /** Pushes up to n items; returns the number actually written (drops the rest). */
    int push (const T* src, int n) noexcept
    {
        int s1, z1, s2, z2;
        fifo.prepareToWrite (n, s1, z1, s2, z2);
        for (int i = 0; i < z1; ++i) storage[(size_t) (s1 + i)] = src[i];
        for (int i = 0; i < z2; ++i) storage[(size_t) (s2 + i)] = src[z1 + i];
        fifo.finishedWrite (z1 + z2);
        return z1 + z2;
    }

    int pop (T* dst, int n) noexcept
    {
        int s1, z1, s2, z2;
        fifo.prepareToRead (n, s1, z1, s2, z2);
        for (int i = 0; i < z1; ++i) dst[i] = storage[(size_t) (s1 + i)];
        for (int i = 0; i < z2; ++i) dst[z1 + i] = storage[(size_t) (s2 + i)];
        fifo.finishedRead (z1 + z2);
        return z1 + z2;
    }

    /** Consumer side: discards everything currently readable. */
    void clear() noexcept
    {
        int s1, z1, s2, z2;
        fifo.prepareToRead (fifo.getNumReady(), s1, z1, s2, z2);
        fifo.finishedRead (z1 + z2);
    }

private:
    juce::AbstractFifo fifo { 1 };
    std::vector<T> storage;
};
} // namespace ff
