#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace ff::theory
{
/** MIDI note 69 = A4. */
double midiToFrequency (double midiNote, double a4 = 440.0) noexcept;
double frequencyToMidi (double hz, double a4 = 440.0) noexcept;

/** Pitch class 0..11 (C = 0). Sharps by default, flats if requested. */
juce::String pitchClassName (int pitchClass, bool useFlats = false);
/** "E2", "A#4", ... (scientific pitch notation, C4 = MIDI 60). */
juce::String midiNoteName (int midiNote, bool useFlats = false);
/** Parses "E2", "Bb1", "f#3". Returns -1 on failure. */
int parseNoteName (const juce::String& text);

struct NearestNote
{
    int midiNote = -1;     // nearest equal-tempered note
    double cents = 0.0;    // deviation from that note, -50..+50
};
NearestNote nearestNote (double hz, double a4 = 440.0) noexcept;

struct Tuning
{
    juce::String name;
    juce::String category;     // "Guitar 6", "Guitar 7", "Guitar 8", "Bass 4", "Bass 5"
    std::vector<int> strings;  // MIDI notes, lowest (thickest) string first
};

/** Built-in tuning presets for 4..8 string instruments. */
const std::vector<Tuning>& tuningPresets();

/** Index of the open string closest (in cents) to the given pitch, or -1. */
int nearestStringIndex (const Tuning& tuning, double hz, double a4 = 440.0) noexcept;
} // namespace ff::theory
