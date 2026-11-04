#pragma once
#include "PluginHost.h"
#include "core/RigModel.h"
#include <array>
#include <atomic>

namespace ff::fx
{
/** A track's insert plugins (after its amp rig, before the fader), processed in stereo in a user-defined order.

    Plugins live in fixed physical slots; the play order is a separate packed array, so reordering never reloads a
    plugin. Threading follows AmpRig: setPlugin / setOrder / bypass from the message thread, prepare() only while no
    audio callback is processing this chain, process() real-time safe. */
class InsertChain
{
public:
    static constexpr int maxSlots = rig::maxInserts;

    void prepare (double sampleRate, int maxBlock);
    double getPreparedRate() const noexcept { return preparedRate.load(); }
    bool isPreparedFor (double rate) const noexcept { return rate > 0.0 && std::abs (preparedRate.load() - rate) < 0.01; }

    /** Returns the previous plugin of the physical slot (free it after the next audio callback). */
    std::shared_ptr<juce::AudioPluginInstance> setPlugin (int physicalSlot, std::shared_ptr<juce::AudioPluginInstance>);
    std::shared_ptr<juce::AudioPluginInstance> getPlugin (int physicalSlot) const;
    /** Play order as physical slots; `count` entries are used. */
    void setOrder (const std::array<int, maxSlots>& order, int count) noexcept;
    std::array<std::atomic<bool>, maxSlots> bypass {};

    void process (float* left, float* right, int numSamples) noexcept;
    /** Sum of the latencies of the plugins in the order that are not bypassed. Audio thread. */
    int latency() const noexcept;

private:
    struct Slot
    {
        std::shared_ptr<juce::AudioPluginInstance> owner;
        std::atomic<juce::AudioPluginInstance*> active { nullptr };
    };
    std::array<Slot, maxSlots> slots;
    std::atomic<juce::uint64> order { 0 };   // 4 bits per position, top byte = count
    std::atomic<double> preparedRate { 0.0 };
    double sr = 48000.0;
    int maxBlock = 0;
    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer midi;
};
} // namespace ff::fx
