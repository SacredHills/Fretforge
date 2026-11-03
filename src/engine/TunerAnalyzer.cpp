#include "TunerAnalyzer.h"
#include <algorithm>
#include <cmath>

namespace ff
{
TunerAnalyzer::TunerAnalyzer (SpscFifo<float>& source, std::function<double()> provider)
    : juce::Thread ("FF Tuner Analysis"), fifo (source), getSampleRate (std::move (provider))
{
    scratch.resize (8192);
}

TunerAnalyzer::~TunerAnalyzer() { shutdown(); }

void TunerAnalyzer::start() { startThread (juce::Thread::Priority::normal); }

void TunerAnalyzer::shutdown() { stopThread (2000); }

void TunerAnalyzer::reprepare (double sr)
{
    preparedRate = sr;
    detector.prepare (sr, 27.0, 1600.0);
    const int w = detector.windowSize();
    history.assign ((size_t) w, 0.0f);
    window.assign ((size_t) w, 0.0f);
    writePos = 0;
    newSamples = 0;
    hop = juce::jmax (256, (int) (sr / 40.0));
    recentCount = recentPos = 0;
}

void TunerAnalyzer::run()
{
    while (! threadShouldExit())
    {
        wait (10);

        const double sr = getSampleRate ? getSampleRate() : 0.0;
        if (sr <= 0.0)
        {
            fifo.clear();
            continue;
        }
        if (! juce::exactlyEqual (sr, preparedRate))
            reprepare (sr);

        if (! active.load())
        {
            fifo.clear();
            frequency = 0.0;
            continue;
        }

        const int w = (int) history.size();
        int got;
        while ((got = fifo.pop (scratch.data(), (int) scratch.size())) > 0)
        {
            for (int i = 0; i < got; ++i)
            {
                history[(size_t) writePos] = scratch[(size_t) i];
                writePos = (writePos + 1) % w;
            }
            newSamples += got;
        }

        if (newSamples < hop)
            continue;
        newSamples = 0;

        // unroll the ring buffer, oldest sample first
        for (int i = 0; i < w; ++i)
            window[(size_t) i] = history[(size_t) ((writePos + i) % w)];

        const auto r = detector.detect (window.data());
        level = r.rms;
        clarity = r.clarity;

        const bool good = r.frequency > 0.0 && r.clarity >= minClarity.load() && r.rms >= minLevel.load();
        if (good)
        {
            // Reject single-frame octave jumps by median-filtering the last few readings.
            recent[recentPos] = r.frequency;
            recentPos = (recentPos + 1) % 5;
            recentCount = juce::jmin (5, recentCount + 1);
            double sorted[5];
            for (int i = 0; i < recentCount; ++i)
            {
                // insertion sort of at most five values
                int j = i;
                for (; j > 0 && sorted[j - 1] > recent[i]; --j)
                    sorted[j] = sorted[j - 1];
                sorted[j] = recent[i];
            }
            const double median = sorted[recentCount / 2];
            // If the newest reading agrees with the median within 30 cents, trust the newest
            // (responsive), otherwise report the median (stable).
            const double cents = 1200.0 * std::log2 (r.frequency / median);
            frequency = std::abs (cents) < 30.0 ? r.frequency : median;
        }
        else
        {
            recentCount = 0;
            frequency = 0.0;
        }
        updateCounter.fetch_add (1);
    }
}
} // namespace ff
