#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <vector>

/** Training centre: a rule-based guitar exercise generator. Each technique has a rule that builds a pattern from a
    scale / chord at a fret position; a seed varies the pattern (order, permutations, shapes) so practice does not
    turn into memorising one lick. The result is an ordinary tab SCORE (see TabModel.h), so it plays with the tab synth
    and can be shown or copied like any other tab. */
namespace ff::training
{
struct Technique
{
    const char* id;
    const char* name;
    const char* category;   // Picking, Legato, Rhythm, Fretting hand, Expression, Scales & arpeggios
    const char* tip;        // what to focus on
    int startBpm, goalBpm;  // a sensible tempo trainer range (16ths unless the technique says otherwise)
};
const std::vector<Technique>& techniques();
int techniqueIndex (const juce::String& id);   // -1 if unknown

struct Settings
{
    int technique = 0;
    int position = 5;          // fret of the lowest note of the pattern (5 = A on the low E string); scale shapes: the
                               // key (the root's fret on the low E string of standard tuning)
    int bars = 2;
    int subdivision = 16;      // 8 (eighths), 16 (sixteenths) or 12 (eighth-note triplets)
    juce::uint32 seed = 1;     // pattern variation
    std::vector<int> tuning { 40, 45, 50, 55, 59, 64 };   // MIDI notes, lowest string first (standard E)
};

/** One step of an exercise: a note (or a chord, or a rest) one subdivision long. */
struct Step
{
    struct Note { int string, fret; bool hammer = false, palmMute = false, vibrato = false, tap = false, accent = false, slide = false; float bend = 0.0f;
                  const char* degree = nullptr; };   // degree: scale-degree label for diagrams ("1", "b3"...)
    std::vector<Note> notes;   // empty = rest
};

/** The pattern of steps for the settings (one cycle; the score repeats it to fill the bars). */
std::vector<Step> pattern (const Settings&);

/** A complete SCORE tree (one guitar track) for the settings at `bpm`. */
juce::ValueTree buildScore (const Settings&, double bpm);

/** Scale techniques: every note of the shape (with its degree; "1" = the root), for a fretboard diagram. Empty for
    techniques that are not a scale shape. */
std::vector<Step::Note> scaleDiagram (const Settings&);

/** Plain-text tab of the pattern (for copying / tests). */
juce::String asciiTab (const Settings&);

// ---- practice plans ----
struct Plan
{
    const char* name;
    const char* description;
    std::vector<const char*> techniques;   // technique ids, in order
};
const std::vector<Plan>& plans();
} // namespace ff::training
