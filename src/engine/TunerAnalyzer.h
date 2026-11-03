#pragma once
#include <juce_core/juce_core.h>
#include "core/PitchDetector.h"
#include "core/SpscFifo.h"
#include <atomic>
#include <functional>

namespace ff
{
/** Background analysis thread: drains the engine's analysis FIFO and runs pitch
    detection at roughly 40 Hz. Results are published through atomics so the UI can
    poll them without locks. When inactive it just discards incoming audio. */
class TunerAnalyzer : private juce::Thread
{
public:
    TunerAnalyzer (SpscFifo<float>& source, std::function<double()> sampleRateProvider);
    ~TunerAnalyzer() override;

    void start();
    void shutdown();

    std::atomic<bool> active { false };          // set by UI while a tuner is visible
    std::atomic<float> minClarity { 0.80f };     // confidence gate
    std::atomic<float> minLevel { 0.0015f };     // ~ -56 dBFS

    // results
    std::atomic<double> frequency { 0.0 };      // smoothed, 0 when no stable pitch
    std::atomic<double> clarity { 0.0 };
    std::atomic<double> level { 0.0 };
    std::atomic<juce::uint32> updateCounter { 0 };

private:
    void run() override;
    void reprepare (double sr);

    SpscFifo<float>& fifo;
    std::function<double()> getSampleRate;
    PitchDetector detector;
    double preparedRate = 0.0;

    std::vector<float> history, window, scratch;
    int writePos = 0, newSamples = 0, hop = 1024;
    double recent[5] {};
    int recentCount = 0, recentPos = 0;
};
} // namespace ff
