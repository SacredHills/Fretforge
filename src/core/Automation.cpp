#include "Automation.h"
#include "Timeline.h"
#include <algorithm>
#include <cmath>

namespace ff::automation
{
juce::String pluginParam (int slot, int index) { return "plugin:" + juce::String (slot) + ":" + juce::String (index); }

bool parsePluginParam (const juce::String& p, int& slot, int& index)
{
    if (! p.startsWith ("plugin:")) return false;
    const auto parts = juce::StringArray::fromTokens (p, ":", "");
    if (parts.size() != 3) return false;
    slot = parts[1].getIntValue();
    index = parts[2].getIntValue();
    return slot >= 0 && index >= 0;
}

std::vector<Point> parse (const juce::String& text)
{
    std::vector<Point> out;
    auto p = text.getCharPointer();
    while (! p.isEmpty())
    {
        Point pt;
        pt.time = juce::CharacterFunctions::readDoubleValue (p);
        if (*p == ',') ++p;
        pt.value = (float) juce::CharacterFunctions::readDoubleValue (p);
        while (! p.isEmpty() && *p != ';') ++p;
        if (*p == ';') ++p;
        out.push_back (pt);
    }
    std::stable_sort (out.begin(), out.end(), [] (const Point& a, const Point& b) { return a.time < b.time; });
    return out;
}

juce::String format (const std::vector<Point>& pts)
{
    juce::String s;
    for (auto& p : pts) s << juce::String (p.time, 4) << ',' << juce::String (p.value, 4) << ';';
    return s;
}

float valueAt (const std::vector<Point>& pts, double t, float fallback) noexcept
{
    if (pts.empty()) return fallback;
    if (t <= pts.front().time) return pts.front().value;
    if (t >= pts.back().time) return pts.back().value;
    auto it = std::upper_bound (pts.begin(), pts.end(), t, [] (double x, const Point& p) { return x < p.time; });
    const auto& b = *it;
    const auto& a = *(it - 1);
    const double span = b.time - a.time;
    if (span <= 1.0e-9) return b.value;
    return a.value + (float) ((t - a.time) / span) * (b.value - a.value);
}

juce::ValueTree lane (juce::ValueTree track, const juce::String& p, bool create, juce::UndoManager* um)
{
    auto a = track.getChildWithName (AUTOMATION);
    if (! a.isValid())
    {
        if (! create) return {};
        a = juce::ValueTree (AUTOMATION);
        track.appendChild (a, um);
    }
    for (auto l : a)
        if (l[param].toString() == p) return l;
    if (! create) return {};
    juce::ValueTree l (LANE);
    l.setProperty (param, p, nullptr);
    l.setProperty (points, juce::String(), nullptr);
    a.appendChild (l, um);
    return l;
}

std::vector<Point> pointsOf (const juce::ValueTree& l) { return parse (l[points].toString()); }

void setPoints (juce::ValueTree l, const std::vector<Point>& pts, juce::UndoManager* um)
{
    auto sorted = pts;
    std::stable_sort (sorted.begin(), sorted.end(), [] (const Point& a, const Point& b) { return a.time < b.time; });
    l.setProperty (points, format (sorted), um);
}

bool offsetLane (juce::ValueTree track, const juce::String& param, float delta, float lo, float hi, juce::UndoManager* um)
{
    auto l = lane (track, param, false, nullptr);
    if (! l.isValid()) return false;
    auto pts = pointsOf (l);
    if (pts.empty()) return false;
    for (auto& p : pts) p.value = juce::jlimit (lo, hi, p.value + delta);
    setPoints (l, pts, um);
    return true;
}

bool hasVolumeAutomation (const juce::ValueTree& track)
{
    auto l = lane (track, volumeParam, false, nullptr);
    return l.isValid() && l[points].toString().isNotEmpty();
}

float volumeAt (const juce::ValueTree& track, double time)
{
    const float fader = (float) (double) track[timeline::ids::gainDb];
    auto l = lane (track, volumeParam, false, nullptr);
    return l.isValid() ? valueAt (pointsOf (l), time, fader) : fader;
}

juce::Array<juce::ValueTree> activeLanes (const juce::ValueTree& track)
{
    juce::Array<juce::ValueTree> out;
    for (auto l : track.getChildWithName (AUTOMATION))
        if (l[points].toString().isNotEmpty()) out.add (l);
    return out;
}

float toNormal (const juce::String& p, float v)
{
    if (p == volumeParam) return std::sqrt (juce::jlimit (0.0f, 1.0f, (v + 60.0f) / 66.0f));
    if (p == panParam) return juce::jlimit (0.0f, 1.0f, (v + 1.0f) * 0.5f);
    return juce::jlimit (0.0f, 1.0f, v);
}

float fromNormal (const juce::String& p, float n)
{
    n = juce::jlimit (0.0f, 1.0f, n);
    if (p == volumeParam) return n * n * 66.0f - 60.0f;
    if (p == panParam) return n * 2.0f - 1.0f;
    return n;
}

juce::String valueText (const juce::String& p, float v)
{
    if (p == volumeParam) return v <= -59.9f ? juce::String ("-inf dB") : juce::String (v, 1) + " dB";
    if (p == panParam) return std::abs (v) < 0.005f ? juce::String ("C") : juce::String (juce::roundToInt (std::abs (v) * 100)) + (v < 0 ? " L" : " R");
    return juce::String (juce::roundToInt (v * 100.0f)) + " %";
}

float defaultValue (const juce::String& p, const juce::ValueTree& track)
{
    if (p == volumeParam) return (float) (double) track[timeline::ids::gainDb];
    if (p == panParam) return (float) (double) track[timeline::ids::pan];
    return 0.5f;
}
} // namespace ff::automation
