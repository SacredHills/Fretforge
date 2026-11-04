#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace ff::fx
{
/** Creates plugin instances (VST3 / AU) for the rigs. The app provides the implementation (format manager, scanning);
    without one, plugin slots stay silent pass-throughs and exports report them. */
class PluginHost
{
public:
    virtual ~PluginHost() = default;
    /** Message thread. `done` is called on the message thread. */
    virtual void createAsync (const juce::String& descriptionXml, double sampleRate, int blockSize,
                              std::function<void (std::unique_ptr<juce::AudioPluginInstance>, juce::String error)> done) = 0;
    /** Any thread except the message thread (it waits for the message thread to create the plugin). */
    virtual std::unique_ptr<juce::AudioPluginInstance> createBlocking (const juce::String& descriptionXml, double sampleRate, int blockSize,
                                                                       juce::String& error) = 0;
};

void setPluginHost (PluginHost*);
PluginHost* pluginHost();

/** Restores a plugin's state from base64 (as stored in a PLUGIN node). */
void restorePluginState (juce::AudioPluginInstance&, const juce::String& base64);
juce::String capturePluginState (juce::AudioPluginInstance&);
/** Runs a plugin on a stereo pair in place (real-time safe). `scratch` must have >= 8 channels and >= n samples. */
void runPluginStereo (juce::AudioPluginInstance&, juce::AudioBuffer<float>& scratch, juce::MidiBuffer& midi,
                      float* left, float* right, int n) noexcept;
/** Asks for a stereo (or else mono) main bus and prepares the plugin. */
void preparePlugin (juce::AudioPluginInstance&, double sampleRate, int blockSize);
} // namespace ff::fx
