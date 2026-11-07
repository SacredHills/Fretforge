#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>

/** MIDI clips and instrument tracks.

    A MIDI clip is a CLIP node with midi = true and no file. Its notes live in a MIDI child as compact text (fast to
    save and to copy with the clip): times are seconds from the start of the clip's content, so trimming and splitting
    work like audio clips (the clip shows content from `offset` for `length` seconds).
    An instrument track's synth / sampler is its INSTRUMENT child { desc, name, state } (like a PLUGIN node). */
namespace ff::midiclip
{
#define FF_MC_ID(n) inline const juce::Identifier n { #n };
FF_MC_ID (midi) FF_MC_ID (MIDI) FF_MC_ID (notes) FF_MC_ID (sustain) FF_MC_ID (channel) FF_MC_ID (INSTRUMENT)
#undef FF_MC_ID

struct Note { double start = 0.0, length = 0.0; int pitch = 60; int velocity = 100; };
struct Pedal { double time = 0.0; bool down = false; };   // sustain pedal (CC 64)

bool isMidiClip (const juce::ValueTree& clip);
std::vector<Note> notesOf (const juce::ValueTree& clip);
std::vector<Pedal> pedalOf (const juce::ValueTree& clip);
void setNotes (juce::ValueTree clip, const std::vector<Note>&, const std::vector<Pedal>&, juce::UndoManager*);
/** A new MIDI clip at `start` holding the notes (times relative to the clip), as long as its last note. */
juce::ValueTree createMidiClip (const juce::String& name, double start, const std::vector<Note>&, const std::vector<Pedal>& = {});
/** Lowest / highest pitch (for drawing); false if there are no notes. */
bool pitchRange (const std::vector<Note>&, int& lo, int& hi);

/** One MIDI file track, ready to become an instrument track. */
struct ImportedTrack
{
    juce::String name;
    int channel = 1;           // most used channel (10 = drums)
    int program = -1;          // first program change, -1 if none
    std::vector<Note> notes;   // absolute seconds
    std::vector<Pedal> pedal;
};
struct ImportedFile
{
    std::vector<ImportedTrack> tracks;
    double firstTempo = 0.0;   // quarter-note BPM of the first tempo event (0 if none)
    int tsNum = 0, tsDen = 0;
    juce::String error;
};
/** Reads a Standard MIDI File (type 0 or 1). Type 0 files are split by channel. */
ImportedFile readMidiFile (const juce::File&);

/** Writes a Standard MIDI File (type 1: a tempo track, then one track of notes). Note / pedal times are seconds from the
    start of the file, at a constant tempo. Returns an error message, or empty on success. */
juce::String writeMidiFile (const juce::File&, const juce::String& trackName, const std::vector<Note>&, const std::vector<Pedal>&,
                            double bpm, int tsNum, int tsDen, int channel = 1);
/** The notes and pedal a MIDI clip plays (inside its trimmed range), in seconds from the clip's start. */
void clipContent (const juce::ValueTree& clip, std::vector<Note>&, std::vector<Pedal>&);

/** General MIDI program name ("Acoustic Grand Piano"...). */
juce::String gmProgramName (int program);
} // namespace ff::midiclip
