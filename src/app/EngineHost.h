#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include "engine/AudioEngine.h"
#include "engine/TunerAnalyzer.h"

namespace ff
{
/** Owns the device manager, the real-time engine and the analysis thread, and
    persists the user's audio/MIDI device configuration. Message-thread object. */
class EngineHost : private juce::ChangeListener, private juce::MidiInputCallback
{
public:
    EngineHost();
    ~EngineHost() override;

    /** Opens the saved device (or the default one). Returns an error message, empty on success. */
    juce::String initialise (const juce::XmlElement* savedState);
    std::unique_ptr<juce::XmlElement> createStateXml() const;

    juce::AudioDeviceManager deviceManager;
    AudioEngine engine;
    TunerAnalyzer tuner;

    bool hasDevice() const;
    juce::String deviceSummary() const;       // "ALSA · hw:0 · 48 kHz · 256 smp · 10.7 ms"
    juce::StringArray activeInputChannelNames() const;
    double cpuUsage() const { return deviceManager.getCpuUsage(); }
    int xruns() const;

    std::atomic<juce::uint32> midiActivity { 0 };
    juce::String lastError;
    std::function<void()> onDeviceChanged;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
};
} // namespace ff
