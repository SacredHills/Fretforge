#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <vector>

/** Track automation: TRACK/AUTOMATION/LANE { param, points }.

    param: "volume" (points in dB), "pan" (-1..1) or "plugin:<slot>:<index>" (a track insert's parameter by the
    insert's physical slot and the parameter index, points normalised 0..1).
    points: "time,value;..." sorted by time (seconds). Between points the value moves in a straight line; before the
    first and after the last point it holds. A lane with no points does nothing. */
namespace ff::automation
{
#define FF_AU_ID(n) inline const juce::Identifier n { #n };
FF_AU_ID (AUTOMATION) FF_AU_ID (LANE) FF_AU_ID (param) FF_AU_ID (points) FF_AU_ID (showLane)
#undef FF_AU_ID

inline const juce::String volumeParam = "volume", panParam = "pan";
juce::String pluginParam (int slot, int index);
bool parsePluginParam (const juce::String& param, int& slot, int& index);

struct Point { double time = 0.0; float value = 0.0f; };
std::vector<Point> parse (const juce::String&);
juce::String format (const std::vector<Point>&);
/** Value at `time` (straight lines between points, held outside them); `fallback` if there are no points. */
float valueAt (const std::vector<Point>&, double time, float fallback) noexcept;

/** The lane for `param` (created when `create`), invalid if none. */
juce::ValueTree lane (juce::ValueTree track, const juce::String& param, bool create, juce::UndoManager*);
std::vector<Point> pointsOf (const juce::ValueTree& lane);
void setPoints (juce::ValueTree lane, const std::vector<Point>&, juce::UndoManager*);
/** Lanes that have points. */
juce::Array<juce::ValueTree> activeLanes (const juce::ValueTree& track);

// value <-> 0..1 height in a lane (volume uses the fader's curve)
float toNormal (const juce::String& param, float value);
float fromNormal (const juce::String& param, float normal);
juce::String valueText (const juce::String& param, float value);
float defaultValue (const juce::String& param, const juce::ValueTree& track);
} // namespace ff::automation
