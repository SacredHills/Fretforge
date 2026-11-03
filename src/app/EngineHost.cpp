#include "EngineHost.h"

namespace ff
{
EngineHost::EngineHost()
    : tuner (engine.analysisFifo(), [this] { return engine.getSampleRate(); })
{
}

EngineHost::~EngineHost()
{
    deviceManager.removeChangeListener (this);
    deviceManager.removeMidiInputDeviceCallback ({}, this);
    deviceManager.removeAudioCallback (&engine);
    deviceManager.closeAudioDevice();
    tuner.shutdown();
}

juce::String EngineHost::initialise (const juce::XmlElement* savedState)
{
    deviceManager.addAudioCallback (&engine);
    deviceManager.addMidiInputDeviceCallback ({}, this); // receives every enabled MIDI input
    deviceManager.addChangeListener (this);

    auto error = deviceManager.initialise (2, 2, savedState, true);
    if (error.isNotEmpty() && savedState != nullptr)
    {
        juce::Logger::writeToLog ("Saved audio device could not be opened (" + error + "); falling back to default.");
        error = deviceManager.initialise (2, 2, nullptr, true);
    }
    if (error.isEmpty() && deviceManager.getCurrentAudioDevice() == nullptr)
        error = "No audio device is available. Connect an interface or choose a device in Settings.";

    lastError = error;
    juce::Logger::writeToLog (error.isEmpty() ? "Audio device opened: " + deviceSummary() : "Audio init: " + error);
    tuner.start();
    return error;
}

std::unique_ptr<juce::XmlElement> EngineHost::createStateXml() const
{
    return deviceManager.createStateXml();
}

bool EngineHost::hasDevice() const
{
    return deviceManager.getCurrentAudioDevice() != nullptr;
}

juce::String EngineHost::deviceSummary() const
{
    auto* d = deviceManager.getCurrentAudioDevice();
    if (d == nullptr)
        return "No audio device";

    const double sr = d->getCurrentSampleRate();
    const int bs = d->getCurrentBufferSizeSamples();
    const double latencyMs = sr > 0 ? (d->getOutputLatencyInSamples() + d->getInputLatencyInSamples() + bs) * 1000.0 / sr : 0.0;
    const auto dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
    return d->getTypeName() + dot + d->getName() + dot + juce::String (sr / 1000.0, 1) + " kHz" + dot
           + juce::String (bs) + " smp" + dot + juce::String (latencyMs, 1) + " ms";
}

juce::StringArray EngineHost::activeInputChannelNames() const
{
    juce::StringArray result;
    if (auto* d = deviceManager.getCurrentAudioDevice())
    {
        const auto names = d->getInputChannelNames();
        const auto active = d->getActiveInputChannels();
        for (int i = 0; i < names.size(); ++i)
            if (active[i])
                result.add (names[i]);
    }
    return result;
}

int EngineHost::xruns() const
{
    if (auto* d = deviceManager.getCurrentAudioDevice())
        return d->getXRunCount();
    return -1;
}

void EngineHost::changeListenerCallback (juce::ChangeBroadcaster*)
{
    lastError.clear();
    juce::Logger::writeToLog ("Audio device changed: " + deviceSummary());
    if (onDeviceChanged)
        onDeviceChanged();
}

void EngineHost::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    if (! m.isActiveSense() && ! m.isMidiClock())
        midiActivity.fetch_add (1);
}
} // namespace ff
