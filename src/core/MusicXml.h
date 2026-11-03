#pragma once
#include "GuitarPro.h"

/** MusicXML (3.x / 4.0, score-partwise) import and export for the tab model.

    Import accepts .musicxml / .xml and compressed .mxl files. Parts written as tablature (staff-tuning plus
    <technical><string>/<fret>) keep their exact fingering; notation-only parts get a fitting tuning and
    automatically assigned strings. Repeats and endings are written out like Guitar Pro imports.
    Only the first voice of the first staff of each part is imported; the rest is reported as warnings.

    Export writes one part per tab track: a TAB staff with the track's tuning and capo, pitches, string/fret
    technical marks, techniques that MusicXML can express, tempo and meter changes, key, chords and lyrics. */
namespace ff::tab::musicxml
{
juce::Result read (const juce::File& file, gp::Song& out);
juce::Result readText (const juce::String& xml, gp::Song& out);
ImportResult importFile (juce::ValueTree score, const juce::File& file, bool replace, juce::UndoManager*);

std::unique_ptr<juce::XmlElement> exportScore (const juce::ValueTree& score);
juce::Result exportFile (const juce::ValueTree& score, const juce::File& file);

/** Pitch spelling for a MIDI note in a key (-7..7): step letter, alter (-1/0/1), octave. */
struct Spelling { char step = 'C'; int alter = 0, octave = 4; };
Spelling spell (int midi, int key);
} // namespace ff::tab::musicxml
