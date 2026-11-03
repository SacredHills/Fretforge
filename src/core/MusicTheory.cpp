#include "MusicTheory.h"
#include <cmath>

namespace ff::theory
{
double midiToFrequency (double midiNote, double a4) noexcept
{
    return a4 * std::pow (2.0, (midiNote - 69.0) / 12.0);
}

double frequencyToMidi (double hz, double a4) noexcept
{
    if (hz <= 0.0 || a4 <= 0.0)
        return 0.0;
    return 69.0 + 12.0 * std::log2 (hz / a4);
}

juce::String pitchClassName (int pc, bool useFlats)
{
    static const char* sharps[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* flats[]  = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    pc = ((pc % 12) + 12) % 12;
    return useFlats ? flats[pc] : sharps[pc];
}

juce::String midiNoteName (int midiNote, bool useFlats)
{
    const int octave = (int) std::floor (midiNote / 12.0) - 1;
    return pitchClassName (midiNote, useFlats) + juce::String (octave);
}

int parseNoteName (const juce::String& textIn)
{
    auto text = textIn.trim();
    if (text.isEmpty())
        return -1;

    static const int base[] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    const auto letter = juce::CharacterFunctions::toUpperCase (text[0]);
    if (letter < 'A' || letter > 'G')
        return -1;

    int pc = base[letter - 'A'];
    int i = 1;
    while (i < text.length() && (text[i] == '#' || text[i] == 'b'))
    {
        pc += text[i] == '#' ? 1 : -1;
        ++i;
    }

    auto octaveText = text.substring (i);
    if (octaveText.isEmpty() || ! octaveText.containsOnly ("-0123456789"))
        return -1;

    const int octave = octaveText.getIntValue();
    const int midi = (octave + 1) * 12 + pc;
    return (midi >= 0 && midi <= 127) ? midi : -1;
}

NearestNote nearestNote (double hz, double a4) noexcept
{
    NearestNote r;
    if (hz <= 0.0)
        return r;
    const double m = frequencyToMidi (hz, a4);
    r.midiNote = (int) std::lround (m);
    r.cents = (m - r.midiNote) * 100.0;
    return r;
}

static Tuning makeTuning (const char* name, const char* cat, std::initializer_list<const char*> notes)
{
    Tuning t { name, cat, {} };
    for (auto* n : notes)
        t.strings.push_back (parseNoteName (n));
    return t;
}

const std::vector<Tuning>& tuningPresets()
{
    static const std::vector<Tuning> presets {
        makeTuning ("Standard E",          "Guitar 6", { "E2", "A2", "D3", "G3", "B3", "E4" }),
        makeTuning ("Eb Standard",         "Guitar 6", { "Eb2", "Ab2", "Db3", "Gb3", "Bb3", "Eb4" }),
        makeTuning ("D Standard",          "Guitar 6", { "D2", "G2", "C3", "F3", "A3", "D4" }),
        makeTuning ("C Standard",          "Guitar 6", { "C2", "F2", "Bb2", "Eb3", "G3", "C4" }),
        makeTuning ("B Standard",          "Guitar 6", { "B1", "E2", "A2", "D3", "F#3", "B3" }),
        makeTuning ("Drop D",              "Guitar 6", { "D2", "A2", "D3", "G3", "B3", "E4" }),
        makeTuning ("Drop C#",             "Guitar 6", { "C#2", "G#2", "C#3", "F#3", "A#3", "D#4" }),
        makeTuning ("Drop C",              "Guitar 6", { "C2", "G2", "C3", "F3", "A3", "D4" }),
        makeTuning ("Drop B",              "Guitar 6", { "B1", "F#2", "B2", "E3", "G#3", "C#4" }),
        makeTuning ("Drop A",              "Guitar 6", { "A1", "E2", "A2", "D3", "F#3", "B3" }),
        makeTuning ("Open G",              "Guitar 6", { "D2", "G2", "D3", "G3", "B3", "D4" }),
        makeTuning ("Open D",              "Guitar 6", { "D2", "A2", "D3", "F#3", "A3", "D4" }),
        makeTuning ("Open E",              "Guitar 6", { "E2", "B2", "E3", "G#3", "B3", "E4" }),
        makeTuning ("DADGAD",              "Guitar 6", { "D2", "A2", "D3", "G3", "A3", "D4" }),
        makeTuning ("Standard B (7)",      "Guitar 7", { "B1", "E2", "A2", "D3", "G3", "B3", "E4" }),
        makeTuning ("Drop A (7)",          "Guitar 7", { "A1", "E2", "A2", "D3", "G3", "B3", "E4" }),
        makeTuning ("Standard A (7)",      "Guitar 7", { "A1", "D2", "G2", "C3", "F3", "A3", "D4" }),
        makeTuning ("Standard F# (8)",     "Guitar 8", { "F#1", "B1", "E2", "A2", "D3", "G3", "B3", "E4" }),
        makeTuning ("Drop E (8)",          "Guitar 8", { "E1", "B1", "E2", "A2", "D3", "G3", "B3", "E4" }),
        makeTuning ("Bass Standard (4)",   "Bass 4",   { "E1", "A1", "D2", "G2" }),
        makeTuning ("Bass Drop D (4)",     "Bass 4",   { "D1", "A1", "D2", "G2" }),
        makeTuning ("Bass D Standard (4)", "Bass 4",   { "D1", "G1", "C2", "F2" }),
        makeTuning ("Bass Standard (5)",   "Bass 5",   { "B0", "E1", "A1", "D2", "G2" }),
        makeTuning ("Bass High C (5)",     "Bass 5",   { "E1", "A1", "D2", "G2", "C3" }),
    };
    return presets;
}

int nearestStringIndex (const Tuning& tuning, double hz, double a4) noexcept
{
    if (hz <= 0.0)
        return -1;
    const double m = frequencyToMidi (hz, a4);
    int best = -1;
    double bestDist = 1.0e9;
    for (size_t i = 0; i < tuning.strings.size(); ++i)
    {
        const double d = std::abs (m - tuning.strings[i]);
        if (d < bestDist)
        {
            bestDist = d;
            best = (int) i;
        }
    }
    return best;
}
} // namespace ff::theory
