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

    enableAllInputsIfNewDevice();
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

juce::StringArray EngineHost::activeOutputChannelNames (juce::Array<int>& channels) const
{
    juce::StringArray result;
    channels.clear();
    if (auto* d = deviceManager.getCurrentAudioDevice())
    {
        const auto names = d->getOutputChannelNames();
        const auto active = d->getActiveOutputChannels();
        for (int i = 0; i < juce::jmin (names.size(), timeline::maxHardwareChannels); ++i)
            if (active[i]) { result.add (names[i]); channels.add (i); }
    }
    return result;
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

void EngineHost::enableAllInputsIfNewDevice()
{
    // JUCE opens only the first two inputs. A guitar app wants every input of the interface (all its analog inputs,
    // ADAT / S/PDIF...), so each newly chosen device starts with all of them on (they can still be switched off in
    // Settings afterwards: that choice is kept until another device is picked).
    auto* d = deviceManager.getCurrentAudioDevice();
    if (d == nullptr) return;
    const auto key = d->getTypeName() + "/" + d->getName();
    if (key == inputsEnabledFor) return;
    inputsEnabledFor = key;
    // the same for outputs, so tracks can be sent to the interface's other outputs (Out 3+4...); the master stays on 1+2
    const int n = d->getInputChannelNames().size();
    const int nOut = juce::jmin (d->getOutputChannelNames().size(), timeline::maxHardwareChannels);
    auto setup = deviceManager.getAudioDeviceSetup();
    const bool inputsDone = n <= 0 || (! setup.useDefaultInputChannels && setup.inputChannels.countNumberOfSetBits() >= n);
    const bool outputsDone = nOut <= 0 || (! setup.useDefaultOutputChannels && setup.outputChannels.countNumberOfSetBits() >= nOut);
    if (inputsDone && outputsDone) return;
    if (! inputsDone)
    {
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();
        setup.inputChannels.setRange (0, n, true);
    }
    if (! outputsDone)
    {
        setup.useDefaultOutputChannels = false;
        setup.outputChannels.clear();
        setup.outputChannels.setRange (0, nOut, true);
    }
    const juce::ScopedValueSetter<bool> svs (changingInputs, true);
    const auto err = deviceManager.setAudioDeviceSetup (setup, true);
    juce::Logger::writeToLog (err.isEmpty() ? "Enabled all " + juce::String (n) + " inputs and " + juce::String (nOut) + " outputs of " + d->getName()
                                            : "Could not enable all inputs / outputs: " + err);
}

void EngineHost::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (changingInputs) return;
    enableAllInputsIfNewDevice();
    lastError.clear();
    juce::Logger::writeToLog ("Audio device changed: " + deviceSummary());
    if (onDeviceChanged)
        onDeviceChanged();
}

void EngineHost::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    if (! m.isActiveSense() && ! m.isMidiClock())
    {
        midiActivity.fetch_add (1);
        engine.pushLiveMidi (m);   // armed instrument tracks play it
    }
}
} // namespace ff
