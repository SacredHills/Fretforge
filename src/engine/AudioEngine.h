#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include "core/MetronomeDsp.h"
#include "core/SpscFifo.h"
#include "core/Timeline.h"
#include "PlaybackGraph.h"
#include "TabSynth.h"
#include <array>
#include <atomic>

namespace ff
{
/** Lock-free level meter. Audio thread accumulates peaks; UI reads and decays. */
struct LevelMeter
{
    std::atomic<float> peak { 0.0f };   // max |x| since the UI last took it
    std::atomic<float> rms { 0.0f };    // RMS of the latest block
    std::atomic<bool> clipped { false }; // sticky until cleared by the UI

    void update (const float* data, int n) noexcept;
    void clear() noexcept { peak = 0.0f; rms = 0.0f; }
    float takePeak() noexcept { return peak.exchange (0.0f); }

    /** Message-thread only: one owner calls uiTake() once per frame, then any number of
        views read uiPeak. */
    void uiTake() noexcept { uiPeak = takePeak(); }
    float uiPeak = 0.0f;
};

/** The real-time audio engine. Owns no device; it is driven either by an
    AudioDeviceManager (as an AudioIODeviceCallback) or offline by tests via process().

    Real-time contract: process() performs no locking, allocation, IO or logging.
    The arrangement arrives as immutable PlaybackGraph snapshots (submitGraph); retired
    snapshots travel back through a lock-free FIFO and are freed by collectGarbage(). */
class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    // ---- transport (any thread) ----
    void play() noexcept { playRequested = true; }
    void stop() noexcept { playRequested = false; recordRequested = false; }
    void togglePlay() noexcept { if (playRequested.load()) stop(); else play(); }
    void returnToZero() noexcept { locate (0); }
    void locate (juce::int64 samples) noexcept { locateRequest = juce::jmax ((juce::int64) 0, samples); }
    void locateSeconds (double s) noexcept { locate ((juce::int64) std::llround (juce::jmax (0.0, s) * juce::jmax (1.0, sampleRate.load()))); }
    bool isPlaying() const noexcept { return playingState.load(); }
    bool isCountingIn() const noexcept { return countingIn.load(); }
    juce::int64 getPositionSamples() const noexcept { return positionSamples.load(); }
    double getPositionSeconds() const noexcept { const double sr = sampleRate.load(); return sr > 0 ? (double) positionSamples.load() / sr : 0.0; }
    double getPositionPpq() const noexcept { return positionPpq.load(); }
    double getSampleRate() const noexcept { return sampleRate.load(); }
    int getBlockSize() const noexcept { return blockSize.load(); }

    /** Project tempo (quarter-note BPM) and time signature. */
    void setTempo (double bpm) noexcept { metronome.bpm = juce::jlimit (10.0, 999.0, bpm); }
    void setTimeSignature (int num, int den) noexcept;

    // ---- loop / punch (seconds) ----
    std::atomic<bool> loopEnabled { false }, punchEnabled { false };
    std::atomic<double> loopStartSec { 0.0 }, loopEndSec { 8.0 };

    // ---- recording ----
    /** Starts/stops recording on armed tracks that have a recorder in the current graph.
        If the transport is stopped, starting also starts playback after an optional count-in. */
    void setRecording (bool shouldRecord, juce::int64 countInSamples = 0) noexcept;
    bool isRecordRequested() const noexcept { return recordRequested.load(); }
    /** True while the audio thread is writing (or about to write) recorded audio. */
    bool isRecordingActive() const noexcept { return recordAck.load(); }

    // ---- metronome ----
    MetronomeDsp metronome;
    std::atomic<bool> clickWhilePlaying { false }; // DAW click
    std::atomic<bool> practiceMetronome { false };   // free-running metronome when transport is stopped

    // ---- mix / IO ----
    std::atomic<float> masterGain { 1.0f };          // linear, 0..2
    std::atomic<int> analysisInputChannel { 0 };     // input feeding tuner & input meter
    LevelMeter inputMeter, outputMeterL, outputMeterR;
    std::array<LevelMeter, timeline::maxTracks> trackMeters;
    std::atomic<int> activeInputs { 0 }, activeOutputs { 0 };

    /** Mono audio from the analysis input channel for tuner / analysis threads. */
    SpscFifo<float>& analysisFifo() noexcept { return analysis; }

    // ---- arrangement ----
    /** Message thread: hands a new snapshot to the audio thread. */
    void submitGraph (std::unique_ptr<PlaybackGraph> graph);
    /** Message thread: frees snapshots the audio thread has finished with. */
    void collectGarbage();

    // ---- tablature playback ----
    /** When true the engine plays the tab sequence (guitar synth) instead of the arrangement. */
    std::atomic<bool> tabMode { false };
    void submitTabSequence (std::unique_ptr<TabSequence> seq);
    /** Starts playback after `countInSamples` of metronome count-in. */
    void playWithCountIn (juce::int64 countInSamples) noexcept { countInRequest = juce::jmax ((juce::int64) 0, countInSamples); playRequested = true; }

    // ---- driving ----
    void prepare (double sampleRate, int maximumBlockSize);
    void process (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numSamples) noexcept;

    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs,
                                           int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String& message) override;

    std::atomic<juce::uint32> deviceErrorCounter { 0 };

private:
    std::atomic<bool> playRequested { false }, recordRequested { false }, recordAck { false };
    std::atomic<juce::int64> locateRequest { -1 }, countInRequest { 0 };
    std::atomic<bool> playingState { false }, countingIn { false };
    std::atomic<juce::int64> positionSamples { 0 };
    std::atomic<double> positionPpq { 0.0 };
    std::atomic<double> sampleRate { 0.0 };
    std::atomic<int> blockSize { 0 };

    // audio-thread state
    bool playing = false, practiceRunning = false, wasRecording = false;
    juce::int64 pos = 0, countInRemaining = 0;
    juce::SmoothedValue<float> smoothedGain;
    std::vector<float> scratchL, scratchR, silence;
    PlaybackGraph* graph = nullptr;

    std::atomic<PlaybackGraph*> pendingGraph { nullptr };
    std::atomic<TabSequence*> pendingTab { nullptr };
    TabSequence* tabSeq = nullptr;
    SpscFifo<TabSequence*> retiredTabs;
    GuitarSynth synth;
    size_t tabNext = 0;
    bool tabNeedsSeek = true;
    void renderTab (float* const* bus, int numBus, int offset, int len) noexcept;
    SpscFifo<PlaybackGraph*> retired;
    SpscFifo<float> analysis;

    double beatsAt (juce::int64 samplePos) const noexcept;
    void closeRecordSegments() noexcept;
    void renderSegment (const float* const* inputs, int numInputs, float* const* bus, int numBus,
                        int offset, int len, bool rolling, bool recordingNow) noexcept;
};
} // namespace ff
