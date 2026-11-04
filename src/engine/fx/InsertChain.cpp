#include "InsertChain.h"

namespace ff::fx
{
void InsertChain::prepare (double rate, int block)
{
    sr = rate;
    maxBlock = juce::jmax (16, block);
    scratch.setSize (8, maxBlock);
    midi.ensureSize (256);
    for (auto& s : slots)
        if (s.owner != nullptr) preparePlugin (*s.owner, rate, maxBlock);
    preparedRate = rate;
}

std::shared_ptr<juce::AudioPluginInstance> InsertChain::setPlugin (int slot, std::shared_ptr<juce::AudioPluginInstance> p)
{
    if (! juce::isPositiveAndBelow (slot, maxSlots)) return p;
    auto& s = slots[(size_t) slot];
    auto old = std::move (s.owner);
    s.owner = std::move (p);
    s.active.store (s.owner.get(), std::memory_order_release);
    return old;
}

std::shared_ptr<juce::AudioPluginInstance> InsertChain::getPlugin (int slot) const
{
    return juce::isPositiveAndBelow (slot, maxSlots) ? slots[(size_t) slot].owner : nullptr;
}

void InsertChain::setOrder (const std::array<int, maxSlots>& o, int count) noexcept
{
    juce::uint64 v = (juce::uint64) juce::jlimit (0, maxSlots, count) << 56;
    for (int i = 0; i < maxSlots; ++i) v |= (juce::uint64) (o[(size_t) i] & 0xf) << (4 * i);
    order.store (v, std::memory_order_release);
}

int InsertChain::latency() const noexcept
{
    const auto v = order.load (std::memory_order_acquire);
    const int count = (int) (v >> 56);
    int l = 0;
    for (int i = 0; i < count; ++i)
    {
        const int slot = (int) ((v >> (4 * i)) & 0xf);
        if (slot >= maxSlots || bypass[(size_t) slot].load (std::memory_order_relaxed)) continue;
        if (auto* p = slots[(size_t) slot].active.load (std::memory_order_acquire)) l += juce::jmax (0, p->getLatencySamples());
    }
    return l;
}

void InsertChain::process (float* L, float* R, int n) noexcept
{
    if (! isPreparedFor (sr) || maxBlock == 0) return;
    const auto v = order.load (std::memory_order_acquire);
    const int count = (int) (v >> 56);
    for (int done = 0; done < n;)
    {
        const int len = juce::jmin (maxBlock, n - done);
        for (int i = 0; i < count; ++i)
        {
            const int slot = (int) ((v >> (4 * i)) & 0xf);
            if (slot >= maxSlots || bypass[(size_t) slot].load (std::memory_order_relaxed)) continue;
            if (auto* p = slots[(size_t) slot].active.load (std::memory_order_acquire))
                runPluginStereo (*p, scratch, midi, L + done, R + done, len);
        }
        done += len;
    }
}
} // namespace ff::fx
