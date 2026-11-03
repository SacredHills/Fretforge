#include "PluginHost.h"

namespace ff::fx
{
namespace { std::atomic<PluginHost*> theHost { nullptr }; }

void setPluginHost (PluginHost* h) { theHost.store (h); }
PluginHost* pluginHost() { return theHost.load(); }

void restorePluginState (juce::AudioPluginInstance& p, const juce::String& base64)
{
    if (base64.isEmpty()) return;
    juce::MemoryBlock mb;
    if (mb.fromBase64Encoding (base64) && mb.getSize() > 0)
        p.setStateInformation (mb.getData(), (int) mb.getSize());
}

juce::String capturePluginState (juce::AudioPluginInstance& p)
{
    juce::MemoryBlock mb;
    p.getStateInformation (mb);
    return mb.getSize() > 0 ? mb.toBase64Encoding() : juce::String();
}

void preparePlugin (juce::AudioPluginInstance& p, double sampleRate, int blockSize)
{
    // guitar chain: stereo in/out if the plugin can, otherwise mono, otherwise whatever it has
    auto layout = p.getBusesLayout();
    if (p.getBusCount (true) > 0 || p.getBusCount (false) > 0)
    {
        juce::AudioProcessor::BusesLayout stereo = layout, mono = layout;
        if (! stereo.inputBuses.isEmpty()) stereo.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        if (! stereo.outputBuses.isEmpty()) stereo.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        if (! mono.inputBuses.isEmpty()) mono.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
        if (! mono.outputBuses.isEmpty()) mono.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        if (! p.setBusesLayout (stereo)) p.setBusesLayout (mono);
    }
    p.setRateAndBufferSizeDetails (sampleRate, blockSize);
    p.prepareToPlay (sampleRate, blockSize);
}
} // namespace ff::fx
