#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include "DrumSynth.h"
#include "core/TabModel.h"
#include <vector>

namespace ff
{
/** One note for the tablature synth, in seconds (speed already applied). */
struct SynthNote
{
    double startSec = 0.0, endSec = 0.0;
    int track = 0, string = 0;
    float frequency = 110.0f, velocity = 0.8f;
    float bend = 0.0f, slideTo = 0.0f, whammy = 0.0f;
    bool prebend = false, vibrato = false, palmMute = false, dead = false, legato = false, tremolo = false, bass = false;
    int harmonic = 0;
    float tremoloInterval = 0.0625f; // seconds between re-picks when tremolo picking
    float gainL = 0.7f, gainR = 0.7f;
};

/** Immutable playback snapshot for the tab editor (built on the message thread). */
struct TabSequence
{
    struct Click { double sec = 0.0; bool accent = false; };
    std::vector<SynthNote> notes;   // sorted by startSec
    std::vector<Click> clicks;      // metronome beats following the score's meter and tempo changes
    std::vector<DrumHit> drumHits;  // sorted by sec (drum studio / play-along)
    float drumVolume = 0.8f;
    double lengthSec = 0.0;
    double secondsPerQuarter = 0.5; // at the start, after speed scaling
};

/** Drum studio playback: one pattern (or the whole song when `pattern` is invalid) at `bpm`. */
std::unique_ptr<TabSequence> buildDrumSequence (const juce::ValueTree& drums, const juce::ValueTree& pattern, double bpm);

/** Adds the drum song (play-along) to a tab sequence, timed through the score's tempo map. */
void addDrumSong (TabSequence&, const juce::ValueTree& drums, const tab::TempoMap& map);

/** Builds a sequence from a SCORE node. `speed` scales the tempo (0.5 = half speed, pitch unchanged). */
std::unique_ptr<TabSequence> buildTabSequence (const juce::ValueTree& score, double speed);

/** Plucked-string synthesiser (extended Karplus-Strong) with guitar articulations:
    bends, pre-bends, vibrato, slides, palm muting, dead/ghost notes, harmonics,
    hammer-on/pull-off (soft re-excitation), tremolo picking and whammy dives.
    All memory is allocated in prepare(); start/render are real-time safe. */
class GuitarSynth
{
public:
    void prepare (double sampleRate, int maxVoices = 48);
    void allNotesOff() noexcept;                 // fast damp of every voice
    void start (const SynthNote& n) noexcept;    // begins sounding now
    void render (float* left, float* right, int numSamples) noexcept;
    int activeVoices() const noexcept;

private:
    struct Voice
    {
        bool active = false, released = false;
        SynthNote note;
        float* line = nullptr;
        int lineSize = 0, writePos = 0;
        double age = 0.0;            // seconds since start
        float prev = 0.0f, lp = 0.0f, envelope = 0.0f;
        float decayGain = 0.999f, releaseGain = 0.99f, brightness = 0.3f;
        double nextRepick = 0.0;
        juce::uint32 order = 0;
    };

    double sr = 48000.0;
    std::vector<float> storage;
    std::vector<Voice> voices;
    juce::uint32 counter = 0;
    juce::uint32 rng = 0x1234567u;

    float noise() noexcept;
    void excite (Voice&, float amount) noexcept;
    double currentSemis (const Voice&) const noexcept;
};
} // namespace ff
