#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <vector>

/** Guitar/bass tablature model.

    Lives in the project tree:
      SCORE {title, tempo}
        MEASURES / MEASURE {tsNum, tsDen, marker}          (shared measure headers)
        TABTRACKS / TABTRACK {id, name, instrument, tuning, capo, volume, pan, mute, solo}
                      BAR (one per MEASURE, same order)
                        BEAT {dur, dots, tuplet, rest, stroke, tremolo, whammy, text}
                          NOTE {string, fret, techniques...}

    Strings are numbered 0 = lowest (thickest). Durations are note values
    (1 whole, 2 half, 4 quarter ... 64). Time is measured in quarter notes ("q"). */
namespace ff::tab
{
namespace ids
{
    #define FF_TAB_ID(n) inline const juce::Identifier n { #n };
    FF_TAB_ID (SCORE) FF_TAB_ID (MEASURES) FF_TAB_ID (MEASURE) FF_TAB_ID (TABTRACKS) FF_TAB_ID (TABTRACK)
    FF_TAB_ID (BAR) FF_TAB_ID (BEAT) FF_TAB_ID (NOTE)
    FF_TAB_ID (title) FF_TAB_ID (tempo) FF_TAB_ID (tsNum) FF_TAB_ID (tsDen) FF_TAB_ID (marker)
    FF_TAB_ID (id) FF_TAB_ID (name) FF_TAB_ID (instrument) FF_TAB_ID (tuning) FF_TAB_ID (capo)
    FF_TAB_ID (volume) FF_TAB_ID (pan) FF_TAB_ID (mute) FF_TAB_ID (solo)
    FF_TAB_ID (dur) FF_TAB_ID (dots) FF_TAB_ID (tuplet) FF_TAB_ID (rest) FF_TAB_ID (stroke) FF_TAB_ID (tremolo)
    FF_TAB_ID (whammy) FF_TAB_ID (text)
    FF_TAB_ID (string) FF_TAB_ID (fret) FF_TAB_ID (tie) FF_TAB_ID (hammer) FF_TAB_ID (slide) FF_TAB_ID (bend)
    FF_TAB_ID (prebend) FF_TAB_ID (vibrato) FF_TAB_ID (palmMute) FF_TAB_ID (harmonic) FF_TAB_ID (dead) FF_TAB_ID (ghost)
    FF_TAB_ID (letRing) FF_TAB_ID (tap) FF_TAB_ID (staccato) FF_TAB_ID (accent) FF_TAB_ID (finger)
    #undef FF_TAB_ID
}

enum Stroke { strokeNone = 0, strokeDown = 1, strokeUp = 2 };
enum Slide { slideNone = 0, slideTo = 1, slideInBelow = 2, slideOutDown = 3 };
enum Harmonic { harmNone = 0, harmNatural = 1, harmArtificial = 2 };

constexpr int maxFret = 30;

// ---- tunings ----
juce::String tuningToString (const std::vector<int>& midiNotes);
std::vector<int> tuningFromString (const juce::String&);
std::vector<int> trackTuning (const juce::ValueTree& track);
int numStrings (const juce::ValueTree& track);

// ---- creation ----
juce::ValueTree getOrCreateScore (juce::ValueTree project);
juce::ValueTree findScore (const juce::ValueTree& project);
juce::ValueTree addTrack (juce::ValueTree score, const juce::String& name, const std::vector<int>& tuning,
                          const juce::String& instrument, juce::UndoManager*);
juce::ValueTree addMeasure (juce::ValueTree score, int insertIndex, int tsNum, int tsDen, juce::UndoManager*);
void removeMeasure (juce::ValueTree score, int index, juce::UndoManager*);
juce::ValueTree makeBeat (int dur, bool rest);
juce::ValueTree makeNote (int string, int fret);

juce::ValueTree measures (const juce::ValueTree& score);
juce::ValueTree tracks (const juce::ValueTree& score);
int numMeasures (const juce::ValueTree& score);
juce::ValueTree bar (const juce::ValueTree& track, int measure);
juce::ValueTree noteOnString (const juce::ValueTree& beat, int string);

// ---- timing ----
double beatLengthQ (const juce::ValueTree& beat);
double beatLengthQ (int dur, int dots, int tuplet);
double measureLengthQ (const juce::ValueTree& measure);
double barContentQ (const juce::ValueTree& bar);
/** Quarter-note position of each measure's start (size numMeasures + 1). */
std::vector<double> measureStarts (const juce::ValueTree& score);

// ---- compiled playback events ----
struct NoteEvent
{
    double startQ = 0.0, lengthQ = 0.0;   // musical time
    int track = 0, string = 0, midi = 0;  // midi note incl. capo
    float velocity = 0.8f;
    float bend = 0.0f;                    // semitones reached by the end of the bend
    bool prebend = false, vibrato = false, palmMute = false, dead = false, ghost = false;
    bool legato = false;                  // hammer-on / pull-off / tap into this note (soft attack)
    int harmonic = harmNone;
    float slideToSemis = 0.0f;            // pitch glide at the end of the note (slide to next note)
    bool tremolo = false;                 // tremolo picking: re-attacked 32nds
    float whammy = 0.0f;                  // tremolo bar dive (semitones, negative = down)
};
std::vector<NoteEvent> compile (const juce::ValueTree& score);

/** Harmonic pitch offset (semitones) for a natural harmonic at a fret. */
float naturalHarmonicSemis (int fret);

// ---- editing helpers ----
/** Moves frets by `semitones`; notes that would leave 0..maxFret move to a neighbouring string when possible.
    Returns the number of notes that could not be transposed. */
int transposeBeats (const juce::Array<juce::ValueTree>& beats, const std::vector<int>& tuning, int semitones,
                    juce::UndoManager*);

/** Picks playable (string, fret) positions for simultaneous MIDI pitches. Unplayable pitches are skipped. */
std::vector<std::pair<int, int>> assignStrings (const std::vector<int>& midiNotes, const std::vector<int>& tuning,
                                                int preferredFret = 0);

// ---- import / export ----
struct ImportResult
{
    juce::Result result = juce::Result::ok();
    juce::StringArray warnings;
    int tracksAdded = 0;
};

/** Writes a Standard MIDI File (type 1), one MIDI track per tab track, with tempo and meter. */
juce::Result exportMidi (const juce::ValueTree& score, const juce::File& file);
/** Adds tab tracks to `score` from a MIDI file (quantised to 1/16, strings assigned automatically). */
ImportResult importMidi (juce::ValueTree score, const juce::File& file, juce::UndoManager*);
/** Adds a tab track parsed from ASCII tablature text (e|---0---| style). Rhythm is approximated. */
ImportResult importAsciiTab (juce::ValueTree score, const juce::String& text, const juce::String& name, juce::UndoManager*);
} // namespace ff::tab
