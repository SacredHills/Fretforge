#pragma once
#include <juce_core/juce_core.h>
#include <deque>

namespace ff
{
/** Tap-tempo estimator. Feed tap times in milliseconds; returns the averaged BPM
    of the recent taps, or 0 until two taps are registered. A pause longer than
    resetAfterMs starts a new measurement. */
class TapTempo
{
public:
    double tap (double timeMs)
    {
        if (! taps.empty() && timeMs - taps.back() > resetAfterMs)
            taps.clear();
        taps.push_back (timeMs);
        while ((int) taps.size() > maxTaps)
            taps.pop_front();
        if (taps.size() < 2)
            return 0.0;
        const double avg = (taps.back() - taps.front()) / (double) (taps.size() - 1);
        return avg > 0.0 ? juce::jlimit (20.0, 400.0, 60000.0 / avg) : 0.0;
    }

    void reset() { taps.clear(); }
    int numTaps() const { return (int) taps.size(); }

    double resetAfterMs = 2000.0;
    int maxTaps = 8;

private:
    std::deque<double> taps;
};
} // namespace ff
