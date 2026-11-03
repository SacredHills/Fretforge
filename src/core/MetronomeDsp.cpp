#include "MetronomeDsp.h"
#include <cmath>

namespace ff
{
namespace
{
    std::vector<float> makeTone (double sr, double ms, std::initializer_list<std::pair<double, double>> partials,
                                 double decayMs, double noiseAmount, double gain, bool square = false)
    {
        const int n = juce::jmax (1, (int) (sr * ms / 1000.0));
        std::vector<float> buf ((size_t) n);
        juce::Random rnd (1234);
        const double attack = sr * 0.0008; // short fade-in avoids a DC click
        double peak = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            double v = 0.0;
            for (auto [freq, amp] : partials)
            {
                const double s = std::sin (juce::MathConstants<double>::twoPi * freq * t);
                v += amp * (square ? (s >= 0.0 ? 0.6 : -0.6) : s);
            }
            v += noiseAmount * (rnd.nextDouble() * 2.0 - 1.0);
            const double env = std::exp (-t * 1000.0 / decayMs) * juce::jmin (1.0, i / attack);
            buf[(size_t) i] = (float) (v * env);
            peak = juce::jmax (peak, std::abs (v * env));
        }
        if (peak > 0.0)
            for (auto& s : buf)
                s = (float) (s / peak * gain);
        return buf;
    }
}

MetronomeDsp::MetronomeDsp()
{
    for (int i = 0; i < maxBeats; ++i)
        beatLevels[(size_t) i].store (i == 0 ? accent : normal);
}

double MetronomeDsp::beatLengthSamples (double sampleRate, double quarterBpm, int unit) noexcept
{
    const double quarter = sampleRate * 60.0 / juce::jlimit (10.0, 999.0, quarterBpm);
    return quarter * 4.0 / (double) juce::jlimit (1, 32, unit);
}

void MetronomeDsp::prepare (double sampleRate)
{
    sr = sampleRate;
    clicks[electronic] = { makeTone (sr, 40, { { 1975.5, 1.0 }, { 3951.0, 0.25 } }, 9, 0.0, 1.0),
                           makeTone (sr, 40, { { 1318.5, 1.0 }, { 2637.0, 0.2 } }, 9, 0.0, 0.8),
                           makeTone (sr, 30, { { 987.8, 1.0 } }, 7, 0.0, 0.45) };
    clicks[woodblock] = { makeTone (sr, 45, { { 1250.0, 1.0 }, { 2600.0, 0.5 } }, 7, 0.35, 1.0),
                          makeTone (sr, 45, { { 900.0, 1.0 }, { 1900.0, 0.5 } }, 7, 0.35, 0.8),
                          makeTone (sr, 35, { { 700.0, 1.0 }, { 1500.0, 0.4 } }, 5, 0.3, 0.45) };
    clicks[cowbell] = { makeTone (sr, 120, { { 587.0, 1.0 }, { 845.0, 0.8 } }, 40, 0.0, 1.0, true),
                        makeTone (sr, 110, { { 540.0, 1.0 }, { 800.0, 0.8 } }, 35, 0.0, 0.8, true),
                        makeTone (sr, 80, { { 540.0, 1.0 }, { 800.0, 0.8 } }, 20, 0.0, 0.4, true) };
    voice = nullptr;
    reset();
}

void MetronomeDsp::reset() noexcept
{
    samplesToNextTick = 0.0;
    tickInBeat = 0;
    beatInBar = 0;
    barsSinceRamp = 0;
    currentBpm = bpm.load();
    effectiveBpm.store (currentBpm);
    currentBeat.store (-1);
    barCounter.store (0);
    for (auto& m : barMuted) m = false;
}

void MetronomeDsp::resetAtBeat (double beatPosition) noexcept
{
    reset();
    if (beatPosition <= 1.0e-9 || sr <= 0.0)
        return;

    const int beats = juce::jlimit (1, maxBeats, beatsPerBar.load());
    const int sub = juce::jlimit (1, maxSubdivision, subdivision.load());
    const double tickPos = beatPosition * sub;
    const double whole = std::floor (tickPos);
    const juce::int64 nextTick = (juce::int64) whole + ((tickPos - whole) > 1.0e-6 ? 1 : 0);

    tickInBeat = (int) (nextTick % sub);
    beatInBar = (int) ((nextTick / sub) % beats);
    barCounter.store (nextTick / sub / beats);
    samplesToNextTick = ((double) nextTick - tickPos) * beatLengthSamples (sr, currentBpm, beatUnit.load()) / sub;
}

float MetronomeDsp::nextRandom() noexcept
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (float) (rng & 0xFFFFFF) / (float) 0x1000000;
}

void MetronomeDsp::rollBarMutes() noexcept
{
    const float p = randomMuteProbability.load();
    for (int i = 0; i < maxBeats; ++i)
        barMuted[i] = (i > 0 && p > 0.0f) ? (nextRandom() < p) : false; // keep the downbeat as an anchor
}

double MetronomeDsp::nextIntervalSamples() const noexcept
{
    const int sub = juce::jlimit (1, maxSubdivision, subdivision.load());
    const double tick = beatLengthSamples (sr, currentBpm, beatUnit.load()) / sub;
    if (sub % 2 == 0)
    {
        const double sw = juce::jlimit (0.0, 0.5, (double) swing.load());
        // tickInBeat is the tick that was *just* started
        return (tickInBeat % 2 == 0) ? tick * (1.0 + sw) : tick * (1.0 - sw);
    }
    return tick;
}

void MetronomeDsp::startTick() noexcept
{
    const int beats = juce::jlimit (1, maxBeats, beatsPerBar.load());
    if (beatInBar >= beats)
        beatInBar = 0;

    const auto& set = clicks[(size_t) juce::jlimit (0, (int) numSounds - 1, sound.load())];
    const int level = beatLevels[(size_t) beatInBar].load();
    const bool muted = level == mute || barMuted[beatInBar];

    if (tickInBeat == 0)
    {
        currentBeat.store (beatInBar);
        currentBeatMuted.store (muted);
        beatCounter.fetch_add (1);
        voice = muted ? nullptr : (level == accent ? &set.accent : &set.beat);
    }
    else
    {
        voice = muted ? nullptr : &set.sub;
    }
    voicePos = 0;
}

void MetronomeDsp::renderVoice (float* const* outputs, int numChannels, int start, int end, float gain) noexcept
{
    if (voice == nullptr)
        return;
    const int len = (int) voice->size();
    const float* src = voice->data();
    for (int i = start; i < end && voicePos < len; ++i, ++voicePos)
    {
        const float s = src[voicePos] * gain;
        for (int ch = 0; ch < numChannels; ++ch)
            outputs[ch][i] += s;
    }
    if (voicePos >= len)
        voice = nullptr;
}

void MetronomeDsp::triggerClick (bool accented) noexcept
{
    const auto& set = clicks[(size_t) juce::jlimit (0, (int) numSounds - 1, sound.load())];
    voice = accented ? &set.accent : &set.beat;
    voicePos = 0;
    beatCounter.fetch_add (1);
}

void MetronomeDsp::renderTriggered (float* const* outputs, int numChannels, int numSamples) noexcept
{
    renderVoice (outputs, numChannels, 0, numSamples, juce::jlimit (0.0f, 1.0f, volume.load()));
}

void MetronomeDsp::process (float* const* outputs, int numChannels, int numSamples) noexcept
{
    if (sr <= 0.0)
        return;

    if (! rampEnabled.load())
        currentBpm = juce::jlimit (10.0, 999.0, bpm.load());

    const float gain = juce::jlimit (0.0f, 1.0f, volume.load());
    int pos = 0;

    while (samplesToNextTick < (double) numSamples)
    {
        const int offset = juce::jlimit (pos, numSamples - 1, (int) std::ceil (samplesToNextTick - 1.0e-9));
        renderVoice (outputs, numChannels, pos, offset, gain);
        pos = offset;

        startTick();
        const double interval = nextIntervalSamples();

        // advance musical position
        const int sub = juce::jlimit (1, maxSubdivision, subdivision.load());
        if (++tickInBeat >= sub)
        {
            tickInBeat = 0;
            if (++beatInBar >= juce::jlimit (1, maxBeats, beatsPerBar.load()))
            {
                beatInBar = 0;
                barCounter.fetch_add (1);
                rollBarMutes();
                if (rampEnabled.load() && ++barsSinceRamp >= juce::jmax (1, rampEveryBars.load()))
                {
                    barsSinceRamp = 0;
                    const double target = rampTargetBpm.load();
                    const double step = std::abs (rampStepBpm.load());
                    currentBpm = currentBpm < target ? juce::jmin (target, currentBpm + step)
                                                     : juce::jmax (target, currentBpm - step);
                }
            }
        }
        samplesToNextTick += interval;
        effectiveBpm.store (currentBpm);
    }

    renderVoice (outputs, numChannels, pos, numSamples, gain);
    samplesToNextTick -= numSamples;
}
} // namespace ff
