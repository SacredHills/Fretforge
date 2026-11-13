#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <vector>

namespace ff
{
/** Sample-accurate metronome click generator.

    Threading: all public std::atomic members are parameters (written by the UI) or
    status values (written by the audio thread). prepare() allocates and must not be
    called concurrently with process(). process() and reset() are real-time safe.

    Tempo convention: BPM is quarter notes per minute (DAW convention). In 6/8 at
    120 BPM the metronome clicks eighth notes at 240 per minute. */
class MetronomeDsp
{
public:
    static constexpr int maxBeats = 16;
    static constexpr int maxSubdivision = 8;

    enum Level : int { mute = 0, normal = 1, accent = 2 };
    enum Sound : int { electronic = 0, woodblock = 1, cowbell = 2, claves = 3, sticks = 4, hihat = 5, rimshot = 6,
                       beep = 7, kickHat = 8, numSounds = 9 };
    static const char* soundName (int s);

    MetronomeDsp();

    // ---- parameters (UI thread writes) ----
    std::atomic<double> bpm { 120.0 };
    std::atomic<int> beatsPerBar { 4 };
    std::atomic<int> beatUnit { 4 };        // 2, 4, 8, 16
    std::atomic<int> subdivision { 1 };     // clicks per beat, 1..maxSubdivision
    std::atomic<float> swing { 0.0f };      // 0..0.5, applied to even subdivisions
    std::atomic<float> volume { 0.8f };
    std::atomic<int> sound { electronic };
    std::atomic<bool> accentsEnabled { true };   // off: every click is the plain beat sound (no higher-pitched downbeat)
    std::array<std::atomic<int>, maxBeats> beatLevels;

    std::atomic<bool> rampEnabled { false };
    std::atomic<double> rampStepBpm { 5.0 };
    std::atomic<int> rampEveryBars { 4 };
    std::atomic<double> rampTargetBpm { 160.0 };

    /** Rhythm training: probability (0..1) that a non-downbeat beat is silently skipped. */
    std::atomic<float> randomMuteProbability { 0.0f };

    // ---- status (audio thread writes) ----
    std::atomic<double> effectiveBpm { 120.0 };
    std::atomic<int> currentBeat { -1 };
    std::atomic<bool> currentBeatMuted { false };
    std::atomic<juce::uint32> beatCounter { 0 }; // increments on every beat; UI uses it to flash
    std::atomic<juce::int64> barCounter { 0 };

    void prepare (double sampleRate);
    /** Restarts at beat 1 on the next sample. Call from the audio thread. */
    void reset() noexcept;
    /** Restarts so that the click lines up with a musical position measured in beats
        (of the current beat unit) since bar 1, beat 1. Real-time safe. */
    void resetAtBeat (double beatPosition) noexcept;
    /** Adds the click signal into the output channels. */
    void process (float* const* outputs, int numChannels, int numSamples) noexcept;

    /** Externally scheduled clicks (score playback with tempo / meter changes): starts a click now
        (level: accent or normal) and renders only the sounding click without advancing the internal grid. */
    void triggerClick (bool accented) noexcept;
    void renderTriggered (float* const* outputs, int numChannels, int numSamples) noexcept;

    /** Length of one beat in samples at the given tempo/beat unit (testing & UI helper). */
    static double beatLengthSamples (double sampleRate, double quarterBpm, int beatUnit) noexcept;

private:
    struct Click { std::vector<float> accent, beat, sub; };
    std::array<Click, numSounds> clicks;
    double sr = 0.0;

    double samplesToNextTick = 0.0;
    int tickInBeat = 0, beatInBar = 0, barsSinceRamp = 0;
    double currentBpm = 120.0;
    bool barMuted[maxBeats] {};
    juce::uint32 rng = 0x9E3779B9u;

    const std::vector<float>* voice = nullptr;
    int voicePos = 0;

    void renderVoice (float* const* outputs, int numChannels, int start, int end, float gain) noexcept;
    void startTick() noexcept;
    double nextIntervalSamples() const noexcept;
    float nextRandom() noexcept;
    void rollBarMutes() noexcept;
};
} // namespace ff
