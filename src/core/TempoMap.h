#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <algorithm>

namespace ff
{
/** Tempo changes of a project: the base tempo from quarter 0 and steps at later quarter-note positions.
    Converts between musical position (quarter notes from the start) and seconds. A change takes effect at its
    position (a step, no ramp). */
class TempoMap
{
public:
    struct Point { double quarter = 0.0; double bpm = 120.0; };

    TempoMap() { set (120.0, {}); }

    /** `changes` need not be sorted; changes at or before quarter 0 and duplicate positions are ignored. */
    void set (double baseBpm, std::vector<Point> changes)
    {
        std::sort (changes.begin(), changes.end(), [] (const Point& a, const Point& b) { return a.quarter < b.quarter; });
        segs.clear();
        segs.push_back ({ 0.0, 0.0, juce::jlimit (10.0, 999.0, baseBpm) });
        for (const auto& c : changes)
        {
            if (c.quarter <= segs.back().q0 + 1.0e-9) continue;
            const auto& s = segs.back();
            segs.push_back ({ c.quarter, s.s0 + (c.quarter - s.q0) * 60.0 / s.bpm, juce::jlimit (10.0, 999.0, c.bpm) });
        }
    }

    bool isConstant() const noexcept { return segs.size() == 1; }
    int numSegments() const noexcept { return (int) segs.size(); }
    double baseBpm() const noexcept { return segs.front().bpm; }

    double secondsAtQuarter (double q) const noexcept
    {
        const auto& s = segFor (q, true);
        return s.s0 + (q - s.q0) * 60.0 / s.bpm;
    }
    double quarterAtSeconds (double t) const noexcept
    {
        const auto& s = segFor (t, false);
        return s.q0 + (t - s.s0) * s.bpm / 60.0;
    }
    double bpmAtQuarter (double q) const noexcept { return segFor (q, true).bpm; }
    double bpmAtSeconds (double t) const noexcept { return segFor (t, false).bpm; }

private:
    struct Seg { double q0, s0, bpm; };
    std::vector<Seg> segs;

    const Seg& segFor (double v, bool byQuarter) const noexcept
    {
        size_t i = 0;
        for (size_t k = 1; k < segs.size(); ++k)
            if ((byQuarter ? segs[k].q0 : segs[k].s0) <= v + 1.0e-12) i = k; else break;
        return segs[i];
    }
};
} // namespace ff
