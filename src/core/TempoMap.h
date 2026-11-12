#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <algorithm>
#include <cmath>

namespace ff
{
/** The project's tempo as automation over time: the base tempo at second 0, then points (time in seconds, bpm) joined
    by straight lines (a ramp between two different values). Before the first point the tempo is the base tempo, after
    the last it stays at the last value. Converts between seconds and musical position (quarter notes from the start)
    by integrating the tempo. */
class TempoMap
{
public:
    struct Point { double time = 0.0; double bpm = 120.0; };

    TempoMap() { set (120.0, {}); }

    /** `points` need not be sorted; points at or before second 0 are ignored (that is the base tempo). */
    void set (double baseBpm, std::vector<Point> points)
    {
        std::sort (points.begin(), points.end(), [] (const Point& a, const Point& b) { return a.time < b.time; });
        segs.clear();
        pts.clear();
        pts.push_back ({ 0.0, clampBpm (baseBpm) });
        for (auto p : points)
        {
            if (p.time <= pts.back().time + 1.0e-6) continue;
            p.bpm = clampBpm (p.bpm);
            pts.push_back (p);
        }
        double q = 0.0;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            Seg s;
            s.t0 = pts[i].time;
            s.q0 = q;
            s.b0 = pts[i].bpm;
            const bool last = i + 1 == pts.size();
            s.t1 = last ? 1.0e12 : pts[i + 1].time;
            s.slope = last ? 0.0 : (pts[i + 1].bpm - pts[i].bpm) / (s.t1 - s.t0);   // bpm per second
            segs.push_back (s);
            if (! last) q += (s.t1 - s.t0) * (s.b0 + 0.5 * s.slope * (s.t1 - s.t0)) / 60.0;
        }
    }

    bool isConstant() const noexcept { return pts.size() == 1; }
    int numPoints() const noexcept { return (int) pts.size(); }
    double baseBpm() const noexcept { return pts.front().bpm; }

    double bpmAtSeconds (double t) const noexcept
    {
        const auto& s = segForTime (t);
        return s.b0 + s.slope * (juce::jmax (0.0, t) - s.t0);
    }
    double quarterAtSeconds (double t) const noexcept
    {
        t = juce::jmax (0.0, t);
        const auto& s = segForTime (t);
        const double d = t - s.t0;
        return s.q0 + d * (s.b0 + 0.5 * s.slope * d) / 60.0;
    }
    double secondsAtQuarter (double q) const noexcept
    {
        q = juce::jmax (0.0, q);
        size_t i = 0;
        for (size_t k = 1; k < segs.size(); ++k) if (segs[k].q0 <= q + 1.0e-12) i = k; else break;
        const auto& s = segs[i];
        const double dq = (q - s.q0) * 60.0;                       // bpm-seconds
        if (std::abs (s.slope) < 1.0e-9) return s.t0 + dq / s.b0;
        const double disc = juce::jmax (0.0, s.b0 * s.b0 + 2.0 * s.slope * dq);
        return s.t0 + (std::sqrt (disc) - s.b0) / s.slope;
    }
    double bpmAtQuarter (double q) const noexcept { return bpmAtSeconds (secondsAtQuarter (q)); }

private:
    struct Seg { double t0, t1, q0, b0, slope; };
    std::vector<Point> pts;
    std::vector<Seg> segs;
    static double clampBpm (double b) { return juce::jlimit (10.0, 999.0, b); }
    const Seg& segForTime (double t) const noexcept
    {
        size_t i = 0;
        for (size_t k = 1; k < segs.size(); ++k) if (segs[k].t0 <= t + 1.0e-12) i = k; else break;
        return segs[i];
    }
};
} // namespace ff
