#pragma once
#include "TabModel.h"
#include "DrumModel.h"

/** Guitar Pro import.

    Supported: Guitar Pro 3, 4 and 5 (.gp3 .gp4 .gp5, binary), Guitar Pro 6 (.gpx, compressed container
    holding GPIF XML) and Guitar Pro 7/8 (.gp, zip holding GPIF XML).

    All readers produce a format-neutral `gp::Song`, which `applySong` writes into the project's SCORE.
    What does not map onto RAVENYR's tab model is reported as a warning instead of being dropped silently:
    drum tracks (Drum Studio is a later milestone), second voices, grace notes, unusual tuplets.
    Repeats and alternate endings are written out so that playback follows the form of the song. */
namespace ff::tab::gp
{
struct Note
{
    int string = 0, fret = 0;           // string 0 = lowest
    bool tie = false, hammer = false, dead = false, ghost = false, palmMute = false, letRing = false;
    bool vibrato = false, staccato = false, accent = false, tap = false, prebend = false;
    int slide = slideNone, harmonic = harmNone;
    int drumNote = -1;                  // percussion tracks: General MIDI drum note
    float bend = 0.0f;                  // semitones (highest point)
};

struct Beat
{
    int dur = 4, dots = 0, tuplet = 0;
    std::vector<Note> notes;
    int stroke = strokeNone;
    bool tremolo = false;
    float whammy = 0.0f;
    double tempo = 0.0;                 // tempo change starting at this beat (GP3-5 mix table)
    juce::String text, chord, chordFrets, lyric;
};

struct Bar
{
    std::vector<Beat> beats;
};

struct Track
{
    juce::String name;
    std::vector<int> tuning;            // lowest string first, MIDI notes
    int capo = 0, program = 25;
    bool drums = false, bass = false, mute = false, solo = false;
    float volume = 0.8f, pan = 0.0f;
    std::vector<Bar> bars;              // one per master bar
    juce::String lyrics;                // free lyrics text, syllables separated by spaces / hyphens
    int lyricsStartBar = 0;
};

struct MasterBar
{
    int num = 4, den = 4, key = 0;
    bool keyChange = false, repeatStart = false;
    int repeatCount = 0;                // total plays at a repeat end (0 = no repeat end)
    int alternateEndings = 0;           // bit n = played on pass n + 1
    double tempo = 0.0;                 // tempo change at the start of the bar
    juce::String marker;
};

struct Song
{
    juce::String title, artist, album, format;
    double tempo = 120.0;
    std::vector<MasterBar> masterBars;
    std::vector<Track> tracks;
    juce::StringArray warnings;
};

/** Parses any supported Guitar Pro file (format chosen from the content, not the extension). */
juce::Result readFile (const juce::File& file, Song& out);
juce::Result readBinary (const juce::MemoryBlock& data, Song& out);   // GP3 / GP4 / GP5
juce::Result readGpif (const juce::String& xml, Song& out);           // GP6 / GP7 score.gpif
juce::Result readGpx (const juce::MemoryBlock& data, Song& out);      // GP6 container
/** GPX ("BCFZ"/"BCFS") container: returns the named file, e.g. "score.gpif". Empty on failure. */
juce::MemoryBlock extractGpxFile (const juce::MemoryBlock& container, const juce::String& name);

/** Master-bar order after writing out repeats and alternate endings. */
std::vector<int> playbackOrder (const std::vector<MasterBar>& bars, int maxBars = 4000);

/** Writes the song into `score`. With `replace`, the existing tab tracks and bars are removed first and the
    song's title, tempo and meter are adopted; otherwise its tracks are added to the existing score.
    Drum tracks go into the project's Drum Studio (patterns + song sections; `replace` replaces the drum song). */
ImportResult applySong (juce::ValueTree score, const Song& song, bool replace, juce::UndoManager*);

/** The song's drum tracks as bars of hits (repeats written out), for the Drum Studio. */
std::vector<drums::ImportBar> drumBars (const Song&, juce::StringArray& warnings);

/** General MIDI drum note for a Guitar Pro 6 drum element / variation. */
int gp6DrumNote (int element, int variation);

/** Convenience: readFile + applySong. */
ImportResult importFile (juce::ValueTree score, const juce::File& file, bool replace, juce::UndoManager*);

bool isGuitarProFile (const juce::File&);
} // namespace ff::tab::gp
