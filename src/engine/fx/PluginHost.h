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

/** What plugins read as the host's transport (juce::AudioPlayHead): playing / recording / looping, position, tempo, time
    signature, bar start and loop range. Every plugin the app creates is given this one. The audio thread updates it
    before each block (the offline export too); plugins read it while processing. */
class TransportPlayHead final : public juce::AudioPlayHead
{
public:
    struct Info
    {
        bool playing = false, recording = false, looping = false;
        juce::int64 samples = 0;
        double sampleRate = 48000.0;
        double bpm = 120.0, ppq = 0.0;                 // quarter-note tempo, position in quarters
        int tsNum = 4, tsDen = 4;
        double loopStartPpq = 0.0, loopEndPpq = 0.0;
    };
    void update (const Info&) noexcept;
    juce::Optional<PositionInfo> getPosition() const override;
    bool canControlTransport() override { return false; }
private:
    // a sequence lock: the audio thread writes, plugin code (any thread: audio, or a GUI timer) reads a whole Info
    std::atomic<juce::uint32> seq { 0 };
    Info info;
};
TransportPlayHead& transportPlayHead();

void setPluginHost (PluginHost*);
PluginHost* pluginHost();

/** Restores a plugin's state from base64 (as stored in a PLUGIN node). */
void restorePluginState (juce::AudioPluginInstance&, const juce::String& base64);
juce::String capturePluginState (juce::AudioPluginInstance&);
/** Runs a plugin on a stereo pair in place (real-time safe). `scratch` must have >= 8 channels and >= n samples.
    scLeft / scRight (optional): the audio for the plugin's side-chain input; silence where the plugin has one and none is given. */
void runPluginStereo (juce::AudioPluginInstance&, juce::AudioBuffer<float>& scratch, juce::MidiBuffer& midi,
                      float* left, float* right, int n, const float* scLeft = nullptr, const float* scRight = nullptr) noexcept;
/** Asks for a stereo (or else mono) main bus, switches off side-chain buses if it can, and prepares the plugin. */
void preparePlugin (juce::AudioPluginInstance&, double sampleRate, int blockSize);

// ---- crash protection ----
/** Remembers a plugin instance's name and file, so a crash report can name the plugin it happened in, and starts
    watching it for settings changes (see pluginStateNeedsCapture). */
void registerPluginInstance (juce::AudioPluginInstance*, const juce::String& name, const juce::String& fileOrIdentifier);
/** Stops watching an instance that is about to be freed. */
void unregisterPluginInstance (juce::AudioPluginInstance*);

// ---- saving settings only when they changed ----
// Asking a plugin for its whole state can take a long time (samplers, amp sims), so the periodic save only asks
// plugins that reported a change, and backs off for plugins that keep reporting changes without their state changing
// (meters published as parameters).
/** True if the plugin reported a change since the last capture and is due (always true for unregistered plugins). */
bool pluginStateNeedsCapture (const juce::AudioPluginInstance&);
/** Marks a plugin as changed (its window is open or was just closed: some plugins never report changes). */
void markPluginStateChanged (const juce::AudioPluginInstance&);
/** Tell the tracker whether that capture found a different state. */
void pluginStateCaptured (const juce::AudioPluginInstance&, bool stateDiffered);

/** While alive, marks the message thread as being inside a plugin (loading, opening its editor, restoring state...).
    A crash, or the message thread hanging there for several seconds, writes a report naming the plugin. */
class PluginActivity
{
public:
    PluginActivity (const juce::String& name, const juce::String& fileOrIdentifier, const char* what);
    PluginActivity (const juce::AudioPluginInstance&, const char* what);
    ~PluginActivity();
    PluginActivity (const PluginActivity&) = delete;
    PluginActivity& operator= (const PluginActivity&) = delete;
private:
    bool tracked = false, outer = false;
};

/** Installs the crash handler and the hang watchdog; reports go to `reportFile`. Call once at startup. */
void installPluginCrashReporter (const juce::File& reportFile);
void shutdownPluginCrashReporter();

struct PluginCrashReport
{
    bool exists = false;
    juce::String pluginName, fileOrIdentifier, activity, text;
    bool hang = false;
};
/** The report left by the previous run (if any); it is moved aside so it is only reported once. */
PluginCrashReport takePreviousCrashReport (const juce::File& reportFile);
} // namespace ff::fx
