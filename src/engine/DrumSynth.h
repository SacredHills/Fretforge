#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>

namespace ff
{
/** One scheduled drum hit (seconds already include tempo, swing and humanize). */
struct DrumHit
{
    double sec = 0.0;
    int piece = 0;
    float velocity = 0.8f;
};

/** Synthesised drum kit (no samples): kick, snares, hats with choke, toms and cymbals built from
    pitch-swept sines, filtered noise and inharmonic metallic oscillators. Every piece is generated,
    so the kit is freely redistributable. prepare() allocates; trigger() and render() are real-time safe. */
class DrumSynth
{
public:
    void prepare (double sampleRate);
    void trigger (int piece, float velocity) noexcept;
    void render (float* left, float* right, int numSamples) noexcept;
    void allNotesOff() noexcept;
    int activeVoices() const noexcept;
    float volume = 0.8f;

    static constexpr int maxPieces = 32;
    using SampleSet = std::array<const juce::AudioBuffer<float>*, maxPieces>;
    /** Uses a sample (already at the synth's sample rate; mono or stereo) for a piece, or nullptr for the built-in
        sound. Call on the audio thread (or before rendering): voices of that piece stop, the caller owns the buffer
        and must keep it alive until it is replaced. */
    void setSample (int piece, const juce::AudioBuffer<float>* sample) noexcept;

    /** Renders hits offline (for "render to audio track" and tests), optionally with samples. */
    static void renderOffline (const std::vector<DrumHit>& hits, double sampleRate, double lengthSec, float volume,
                               juce::AudioBuffer<float>& out, const SampleSet* samples = nullptr);

private:
    struct Svf
    {
        float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
        void set (double sr, double fc, double q) noexcept;
        void reset() noexcept { ic1 = ic2 = 0; }
        struct Out { float lp, bp, hp; };
        Out process (float x) noexcept;
    };
    struct Voice
    {
        bool active = false;
        int piece = 0;
        float vel = 0, gainL = 0, gainR = 0, choke = 1.0f, chokeRate = 0.0f;
        double t = 0, length = 0;
        std::array<double, 6> phase {};
        double sinePhase = 0, sinePhase2 = 0;
        Svf f1, f2;
        juce::uint32 order = 0;
        const juce::AudioBuffer<float>* sample = nullptr;
        int samplePos = 0;
    };

    double sr = 48000.0;
    std::array<Voice, 40> voices;
    juce::uint32 counter = 0, rng = 0x2545F491u;
    SampleSet samples {};

    float noise() noexcept;
    float sample (Voice&) noexcept;
};
} // namespace ff
