#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/** Vector glyphs for standard notation, drawn from paths so they look the same on every platform
    (no dependency on a music font being installed). All sizes are in staff spaces (`sp` pixels). */
namespace ff::notation
{
/** Treble clef; `gLineY` is the y of the second line from the bottom. */
void drawTrebleClef (juce::Graphics&, float x, float gLineY, float sp);
/** Bass clef; `fLineY` is the y of the second line from the top. */
void drawBassClef (juce::Graphics&, float x, float fLineY, float sp);
/** Small "8" under a clef (octave-transposing guitar / bass clef). */
void drawOctaveMark (juce::Graphics&, float x, float y, float sp);

void drawSharp (juce::Graphics&, float cx, float cy, float sp);
void drawFlat (juce::Graphics&, float cx, float cy, float sp);
void drawNatural (juce::Graphics&, float cx, float cy, float sp);
void drawAccidental (juce::Graphics&, int alter, float cx, float cy, float sp);  // alter: -1 flat, 0 natural, 1 sharp

/** Notehead centred on (cx, cy). filled = quarter or shorter; cross = dead note. */
void drawNotehead (juce::Graphics&, float cx, float cy, float sp, bool filled, bool cross);
/** Rest of a note value (1 whole ... 64) centred horizontally at cx. `staffTop` is the top line. */
void drawRest (juce::Graphics&, int dur, float cx, float staffTop, float sp);
/** Flags on a stem end (count = 1 for eighths, 2 for sixteenths ...). */
void drawFlags (juce::Graphics&, float stemX, float stemEndY, float sp, int count, bool stemUp);
/** Small quarter-note symbol used in tempo marks ("♩ = 120"). */
void drawTempoNote (juce::Graphics&, float x, float baselineY, float size);
} // namespace ff::notation
