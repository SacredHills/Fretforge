#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <algorithm>
#include <cmath>

namespace ff
{
/** The project's tempo as an envelope over musical position: the base tempo at quarter 0, then points (quarter-note
    position, bpm) joined by straight lines in bpm (a ramp between two different values). Two points at the same
    position make an instant change (the earlier one in the list is the value before the jump). Before the first
    point the tempo is the base tempo, after the last it stays at the last value. Points sit on the beat grid, so the
    grid and the points stay together whatever tempo is changed earlier. */
class TempoMap
{
public:
    struct Point { double quarter = 0.0; double bpm = 120.0; };

    TempoMap() { set (120.0, {}); }

    /** `points` need not be sorted (points at the same quarter keep their order); points at or before quarter 0 are ignored. */
    void set (double baseBpm, std::vector<Point> points)
    {
        std::stable_sort (points.begin(), points.end(), [] (const Point& a, const Point& b) { return a.quarter < b.quarter; });
        pts.clear();
        segs.clear();
        pts.push_back ({ 0.0, clampBpm (baseBpm) });
        for (auto p : points)
            if (p.quarter > 1.0e-9) { p.bpm = clampBpm (p.bpm); pts.push_back (p); }
        double t = 0.0;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            Seg s;
            s.q0 = pts[i].quarter;
            s.t0 = t;
            s.b0 = pts[i].bpm;
            const bool last = i + 1 == pts.size();
            const double dq = last ? 0.0 : pts[i + 1].quarter - pts[i].quarter;
            s.k = (last || dq <= 1.0e-12) ? 0.0 : (pts[i + 1].bpm - pts[i].bpm) / dq;     // bpm per quarter
            segs.push_back (s);
            if (! last && dq > 1.0e-12) t += secondsFor (s, dq);
        }
    }

    bool isConstant() const noexcept { return pts.size() == 1; }
    int numPoints() const noexcept { return (int) pts.size(); }
    double baseBpm() const noexcept { return pts.front().bpm; }

    double secondsAtQuarter (double q) const noexcept
    {
        q = juce::jmax (0.0, q);
        const auto& s = segForQuarter (q);
        return s.t0 + secondsFor (s, q - s.q0);
    }
    double quarterAtSeconds (double t) const noexcept
    {
        t = juce::jmax (0.0, t);
        const auto& s = segForTime (t);
        const double tau = t - s.t0;
        if (std::abs (s.k) < 1.0e-9) return s.q0 + s.b0 * tau / 60.0;
        return s.q0 + s.b0 * (std::exp (s.k * tau / 60.0) - 1.0) / s.k;
    }
    double bpmAtQuarter (double q) const noexcept
    {
        q = juce::jmax (0.0, q);
        const auto& s = segForQuarter (q);
        return s.b0 + s.k * (q - s.q0);
    }
    double bpmAtSeconds (double t) const noexcept { return bpmAtQuarter (quarterAtSeconds (t)); }

private:
    struct Seg { double q0, t0, b0, k; };
    std::vector<Point> pts;
    std::vector<Seg> segs;
    static double clampBpm (double b) { return juce::jlimit (10.0, 999.0, b); }
    /** Seconds to travel dq quarters from the start of a segment (tempo changes linearly with the position). */
    static double secondsFor (const Seg& s, double dq) noexcept
    {
        if (std::abs (s.k) < 1.0e-9) return 60.0 * dq / s.b0;
        return 60.0 / s.k * std::log ((s.b0 + s.k * dq) / s.b0);
    }
    const Seg& segForQuarter (double q) const noexcept
    {
        size_t i = 0;
        for (size_t k = 1; k < segs.size(); ++k) if (segs[k].q0 <= q + 1.0e-12) i = k; else break;
        return segs[i];
    }
    const Seg& segForTime (double t) const noexcept
    {
        size_t i = 0;
        for (size_t k = 1; k < segs.size(); ++k) if (segs[k].t0 <= t + 1.0e-12) i = k; else break;
        return segs[i];
    }
};
} // namespace ff
