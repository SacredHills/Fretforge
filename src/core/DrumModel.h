#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <vector>

/** Drum studio model.

    Lives in the project tree:
      DRUMS {humanizeMs, humanizeVel, volume, selected}
        PATTERNS / PATTERN {id, name, tsNum, tsDen, bars, grid, swing}
                    HIT {lane, tick, vel}
        SONG / SECTION {pattern (id), repeats}

    Positions are ticks (960 per quarter note) so changing the editing grid never moves hits.
    `grid` is the number of grid steps per quarter note (2 = 8ths, 3 = 8th triplets, 4 = 16ths,
    6 = 16th triplets, 8 = 32nds). Velocity is 1..127. */
namespace ff::drums
{
namespace ids
{
    #define FF_DRUM_ID(n) inline const juce::Identifier n { #n };
    FF_DRUM_ID (DRUMS) FF_DRUM_ID (PATTERNS) FF_DRUM_ID (PATTERN) FF_DRUM_ID (HIT) FF_DRUM_ID (SONG) FF_DRUM_ID (SECTION)
    FF_DRUM_ID (id) FF_DRUM_ID (name) FF_DRUM_ID (tsNum) FF_DRUM_ID (tsDen) FF_DRUM_ID (bars) FF_DRUM_ID (grid) FF_DRUM_ID (swing)
    FF_DRUM_ID (lane) FF_DRUM_ID (tick) FF_DRUM_ID (vel) FF_DRUM_ID (pattern) FF_DRUM_ID (repeats)
    FF_DRUM_ID (humanizeMs) FF_DRUM_ID (humanizeVel) FF_DRUM_ID (volume) FF_DRUM_ID (selected)
    #undef FF_DRUM_ID
}

constexpr int ppq = 960;

enum Piece
{
    kick, kick2, snare, sideStick, clap, hhClosed, hhPedal, hhOpen,
    tomHigh, tomMid, tomLow, tomFloor, crash, crash2, china, splash, ride, rideBell, cowbell,
    numPieces
};

struct PieceInfo
{
    const char* name;
    int gmNote;       // General MIDI drum note (channel 10)
    int chokeGroup;   // pieces in the same group cut each other off (0 = none)
    float pan;        // -1..1, drummer's perspective
};
const PieceInfo& pieceInfo (int piece);
/** Kit piece for a General MIDI drum note (-1 if unmapped). */
int pieceForGmNote (int note);

// ---- structure ----
juce::ValueTree getOrCreate (juce::ValueTree project);
juce::ValueTree find (const juce::ValueTree& project);
juce::ValueTree patterns (const juce::ValueTree& drums);
juce::ValueTree song (const juce::ValueTree& drums);
juce::ValueTree patternById (const juce::ValueTree& drums, const juce::String& id);
juce::ValueTree makePattern (const juce::String& name, int tsNum, int tsDen, int bars, int grid);
juce::ValueTree addPattern (juce::ValueTree drums, const juce::String& name, int tsNum, int tsDen, int bars, juce::UndoManager*);
juce::ValueTree addSection (juce::ValueTree drums, const juce::String& patternId, int repeats, int index, juce::UndoManager*);

// ---- pattern helpers ----
int barTicks (const juce::ValueTree& pattern);
int lengthTicks (const juce::ValueTree& pattern);
int stepTicks (const juce::ValueTree& pattern);
juce::ValueTree hitAt (const juce::ValueTree& pattern, int lane, int tick, int tolerance = 0);
void setHit (juce::ValueTree pattern, int lane, int tick, int velocity, juce::UndoManager*);
void removeHit (juce::ValueTree pattern, int lane, int tick, juce::UndoManager*);
/** Changes the number of bars, dropping hits past the new end. */
void setBars (juce::ValueTree pattern, int bars, juce::UndoManager*);
/** Replaces all hits of `pattern` with those of `source` (same length assumed). */
void replaceHits (juce::ValueTree pattern, const juce::ValueTree& source, juce::UndoManager*);

// ---- playback events ----
struct Event
{
    double q = 0.0;        // quarter-note position
    double offsetSec = 0;  // humanize timing offset
    int piece = 0;
    float velocity = 0.8f; // 0..1
};
struct Compiled
{
    std::vector<Event> events;   // sorted by q
    double lengthQ = 0.0;
    struct Bar { double q; int num, den; };
    std::vector<Bar> bars;       // meter of every bar (for clicks / display)
};
/** One pattern (repeated `times`), or the whole song when `pattern` is invalid. */
Compiled compile (const juce::ValueTree& drums, const juce::ValueTree& pattern, int times = 1);
/** Swing moves off-beat 8ths (or 16ths when the grid is 16ths or finer) later; amount 0..0.5. */
int swungTick (int tick, double swing, int grid);

// ---- generator ----
enum Genre
{
    genreRock, genreHardRock, genrePop, genrePunk, genreFunk, genreBlues, genreReggae, genreJazz,
    genreMetal, genreThrash, genreDBeat, genreDoom, genreDeath, genreBlackMetal, genreBlast, genreGallop, genreDjent,
    numGenres
};
const char* genreName (int genre);

struct GenOptions
{
    int genre = genreRock;
    bool halfTime = false;
    double complexity = 0.5;   // 0..1: syncopation, ghost notes, open hats, kick density
    bool fill = true;          // fill in the last bar
    bool crashOnOne = true;
    int bars = 2, tsNum = 4, tsDen = 4;
    juce::uint32 seed = 1;     // same options + seed = same groove
};
/** Generates a detached PATTERN (rule-based, deterministic). */
juce::ValueTree generate (const GenOptions&);

// ---- MIDI ----
/** Standard MIDI File on channel 10 (pattern or whole song). */
juce::Result exportMidi (const juce::ValueTree& drums, const juce::ValueTree& patternOrInvalid, double tempo, const juce::File&);
/** Imports a whole MIDI drum file (channel-10 notes, or all notes if none are on 10). Bars are split into
    patterns: repeated phrases (1, 2, 4 or 8 bars) become one pattern used by a section with repeats, identical
    phrases anywhere in the song share a pattern, and the sections are appended to the song arrangement.
    Meter changes are followed; the first tempo is returned for the caller to adopt. */
struct MidiImport
{
    juce::Result result = juce::Result::ok();
    juce::StringArray warnings;
    juce::String firstPatternId;
    int patterns = 0, sections = 0, bars = 0, hits = 0;
    double tempo = 0.0;
};
MidiImport importMidi (juce::ValueTree drums, const juce::File&, juce::UndoManager*);
} // namespace ff::drums
