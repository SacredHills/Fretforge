#pragma once
#include "PluginHost.h"
#include <atomic>

namespace ff::fx
{
/** The instrument (synth / sampler plugin) of an instrument track. Threading like InsertChain: setPlugin from the
    message thread (free the returned plugin after the next audio callback), prepare() only while no audio callback
    is processing it, render() real-time safe. */
class InstrumentSlot
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        block = juce::jmax (1, maxBlock);
        scratch.setSize (32, block, false, true, false);
        preparedRate = sampleRate;
    }
    double getPreparedRate() const noexcept { return preparedRate.load(); }
    /** Bypassed: the instrument makes no sound (notes held at that moment are released). */
    std::atomic<bool> bypassed { false };

    std::shared_ptr<juce::AudioPluginInstance> setPlugin (std::shared_ptr<juce::AudioPluginInstance> p)
    {
        active.store (p.get());
        std::swap (owner, p);
        return p;
    }
    std::shared_ptr<juce::AudioPluginInstance> getPlugin() const { return owner; }
    juce::AudioPluginInstance* activePlugin() const noexcept { return active.load(); }

    /** Adds the instrument's sound for `midi` into L / R (n samples, n <= the prepared block). */
    void render (float* L, float* R, int n, juce::MidiBuffer& midi) noexcept
    {
        auto* p = active.load();
        if (p == nullptr || n > scratch.getNumSamples()) return;
        if (bypassed.load())
        {
            if (! wasBypassed)   // let go of what is sounding, once, and drop the sound it makes
            {
                wasBypassed = true;
                const int c = juce::jmax (p->getTotalNumInputChannels(), p->getTotalNumOutputChannels(), 1);
                if (c <= scratch.getNumChannels())
                {
                    juce::MidiBuffer off;
                    for (int chn = 1; chn <= 16; ++chn) { off.addEvent (juce::MidiMessage::allNotesOff (chn), 0); off.addEvent (juce::MidiMessage::allSoundOff (chn), 0); }
                    juce::AudioBuffer<float> junk (scratch.getArrayOfWritePointers(), c, n);
                    junk.clear();
                    const juce::ScopedLock sl (p->getCallbackLock());
                    if (! p->isSuspended()) p->processBlock (junk, off);
                }
            }
            midi.clear();
            return;
        }
        wasBypassed = false;
        const int ch = juce::jmax (p->getTotalNumInputChannels(), p->getTotalNumOutputChannels(), 1);
        if (ch > scratch.getNumChannels()) return;
        juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), ch, n);
        view.clear();
        {
            const juce::ScopedLock sl (p->getCallbackLock());
            if (p->isSuspended()) return;
            p->processBlock (view, midi);
        }
        const int outs = p->getTotalNumOutputChannels();
        if (outs <= 0) return;
        juce::FloatVectorOperations::add (L, view.getReadPointer (0), n);
        juce::FloatVectorOperations::add (R, view.getReadPointer (outs > 1 ? 1 : 0), n);
    }

private:
    std::shared_ptr<juce::AudioPluginInstance> owner;
    std::atomic<juce::AudioPluginInstance*> active { nullptr };
    juce::AudioBuffer<float> scratch;
    std::atomic<double> preparedRate { 0.0 };
    double sr = 48000.0;
    int block = 512;
    bool wasBypassed = false;   // audio thread only
};
} // namespace ff::fx
