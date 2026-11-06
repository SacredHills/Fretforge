#include "GraphMixer.h"
#include "core/Timeline.h"

namespace ff
{
namespace
{
constexpr float silenceThreshold = 1.0e-5f;   // -100 dBFS

float peakOf (const float* L, const float* R, int n) noexcept
{
    const auto a = juce::FloatVectorOperations::findMinAndMax (L, n);
    const auto b = juce::FloatVectorOperations::findMinAndMax (R, n);
    return juce::jmax (std::abs (a.getStart()), std::abs (a.getEnd()), std::abs (b.getStart()), std::abs (b.getEnd()));
}

void addScaled (float* dst, const float* src, float g, int n) noexcept
{
    if (g == 0.0f) return;
    juce::FloatVectorOperations::addWithMultiply (dst, src, g, n);
}
}

void GraphMixer::prepare (int maxBlock_, double sampleRate)
{
    blockSize = juce::jmax (1, maxBlock_);
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    bufL.assign ((size_t) timeline::maxTracks, std::vector<float> ((size_t) blockSize, 0.0f));
    bufR.assign ((size_t) timeline::maxTracks, std::vector<float> ((size_t) blockSize, 0.0f));
    tmpL.assign ((size_t) blockSize, 0.0f);
    tmpR.assign ((size_t) blockSize, 0.0f);
    active.assign ((size_t) timeline::maxTracks, 0);
    stage.reserve ((size_t) timeline::maxTracks);
}

void GraphMixer::updateLatencies (PlaybackGraph& g) noexcept
{
    // buses come after their sources in `order`, so walk it backwards
    const bool hasOrder = g.order.size() == g.tracks.size();
    for (int k = (int) g.tracks.size(); --k >= 0;)
    {
        auto& t = g.tracks[(size_t) (hasOrder ? g.order[(size_t) k] : k)];
        t.downstreamLatency = g.masterInserts != nullptr ? g.masterInserts->latency() : 0;
        if (juce::isPositiveAndBelow (t.output, (int) g.tracks.size()))
        {
            const auto& bus = g.tracks[(size_t) t.output];
            t.downstreamLatency = trackLatency (bus) + bus.downstreamLatency;   // the bus's own includes the master
        }
    }
}

void GraphMixer::processTrack (PlayTrack& t, float* L, float* R, int n) noexcept
{
    if (t.instrument != nullptr)   // instrument tracks: the synth plays its MIDI, then the plugins (never idle)
    {
        t.instrument->render (L, R, n, t.midiBuffer);
        if (t.inserts != nullptr) t.inserts->process (L, R, n);
        return;
    }
    if (t.rig == nullptr && t.inserts == nullptr) return;
    auto& idle = t.idle;
    if (peakOf (L, R, n) > silenceThreshold) { idle.silentIn = 0; idle.sleeping = false; }
    else idle.silentIn += n;
    if (allowIdle && idle.sleeping)
    {
        juce::FloatVectorOperations::clear (L, n);
        juce::FloatVectorOperations::clear (R, n);
        return;
    }
    if (t.rig != nullptr) t.rig->process (L, R, n);
    if (t.inserts != nullptr) t.inserts->process (L, R, n);
    if (peakOf (L, R, n) > silenceThreshold) idle.quietOut = 0;
    else idle.quietOut += n;
    // nothing coming in and the tails (delay, reverb...) have died away: stop running the effects until sound arrives
    if (idle.silentIn > (juce::int64) (0.25 * sr) && idle.quietOut > (juce::int64) (3.0 * sr))
        idle.sleeping = true;
}

void GraphMixer::processJob (void* ctx, int i) noexcept
{
    auto& c = *static_cast<JobContext*> (ctx);
    auto& self = *c.self;
    const int ti = self.stage[(size_t) i];
    self.processTrack (c.graph->tracks[(size_t) ti], self.bufL[(size_t) ti].data(), self.bufR[(size_t) ti].data(), c.n);
}

void GraphMixer::mix (PlaybackGraph& g, Source& source, float* outL, float* outR, int n) noexcept
{
    const int numTracks = juce::jmin ((int) g.tracks.size(), (int) bufL.size());
    if (numTracks == 0 || n <= 0 || n > blockSize) return;
    updateLatencies (g);

    for (int i = 0; i < numTracks; ++i)
        if (! timeline::isSourceKind (g.tracks[(size_t) i].kind))
        {
            juce::FloatVectorOperations::clear (bufL[(size_t) i].data(), n);
            juce::FloatVectorOperations::clear (bufR[(size_t) i].data(), n);
        }

    // a graph built by hand (tests) may have no order: then it is the track order
    const bool hasOrder = g.order.size() == g.tracks.size();
    auto orderAt = [&] (int k) { return hasOrder ? g.order[(size_t) k] : k; };
    const int numOrder = numTracks;
    for (int p = 0; p < numOrder;)
    {
        const int level = g.tracks[(size_t) orderAt (p)].level;
        int end = p;
        while (end < numOrder && g.tracks[(size_t) orderAt (end)].level == level) ++end;

        // 1. sources (serial: clips, inputs, recording)
        stage.clear();
        for (int k = p; k < end; ++k)
        {
            const int ti = orderAt (k);
            if (ti >= numTracks) continue;
            auto& t = g.tracks[(size_t) ti];
            float* L = bufL[(size_t) ti].data();
            float* R = bufR[(size_t) ti].data();
            if (timeline::isSourceKind (t.kind))
            {
                juce::FloatVectorOperations::clear (L, n);
                juce::FloatVectorOperations::clear (R, n);
            }
            active[(size_t) ti] = source.fill (ti, t, L, R, n) ? 1 : 0;
            if (active[(size_t) ti] && (t.rig != nullptr || t.inserts != nullptr || t.instrument != nullptr)) stage.push_back (ti);
        }

        // 2. effects (parallel)
        JobContext ctx { this, &g, n };
        if (workers != nullptr) workers->run ((int) stage.size(), &GraphMixer::processJob, &ctx);
        else for (int i = 0; i < (int) stage.size(); ++i) processJob (&ctx, i);

        // 3. sends, fader / pan, output (serial)
        for (int k = p; k < end; ++k)
        {
            const int ti = orderAt (k);
            if (ti >= numTracks || ! active[(size_t) ti]) continue;
            auto& t = g.tracks[(size_t) ti];
            float* L = bufL[(size_t) ti].data();
            float* R = bufR[(size_t) ti].data();
            for (const auto& s : t.sends)
            {
                if (! juce::isPositiveAndBelow (s.target, numTracks)) continue;
                addScaled (bufL[(size_t) s.target].data(), L, s.gain * (s.pre ? 1.0f : t.gainL), n);
                addScaled (bufR[(size_t) s.target].data(), R, s.gain * (s.pre ? 1.0f : t.gainR), n);
            }
            // fader / pan (ramped when automation moved them since the last block)
            auto applyGain = [n] (float* x, float from, float to) {
                if (from < 0.0f || std::abs (from - to) < 1.0e-6f) { juce::FloatVectorOperations::multiply (x, to, n); return; }
                const float step = (to - from) / (float) n;
                for (int i = 0; i < n; ++i) x[i] *= from + step * (float) (i + 1);
            };
            applyGain (L, t.prevGainL, t.gainL);
            applyGain (R, t.prevGainR, t.gainR);
            t.prevGainL = t.gainL;
            t.prevGainR = t.gainR;
            source.post (ti, t, L, R, n);
            if (t.outputMuted) continue;
            float* dL = outL;
            float* dR = outR;
            if (juce::isPositiveAndBelow (t.output, numTracks)) { dL = bufL[(size_t) t.output].data(); dR = bufR[(size_t) t.output].data(); }
            juce::FloatVectorOperations::add (dL, L, n);
            juce::FloatVectorOperations::add (dR, R, n);
        }
        p = end;
    }
    if (g.masterInserts != nullptr)   // outL / outR started out silent, so this is the whole mix
        g.masterInserts->process (outL, outR, n);
}
} // namespace ff
