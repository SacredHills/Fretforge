#include "PlaybackGraph.h"
#include "core/Timeline.h"
#include "core/ProjectFile.h"
#include <cmath>

namespace ff
{
using namespace timeline;

int trackLatency (const PlayTrack& t) noexcept
{
    return (t.rig != nullptr ? t.rig->totalLatency() : 0) + (t.inserts != nullptr ? t.inserts->latency() : 0);
}

void panGains (float pan, float gain, float& left, float& right) noexcept
{
    const float theta = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    left = gain * juce::MathConstants<float>::sqrt2 * std::cos (theta);
    right = gain * juce::MathConstants<float>::sqrt2 * std::sin (theta);
}

std::unique_ptr<PlaybackGraph> buildPlaybackGraph (const juce::ValueTree& tracks, const juce::File& mediaBundle,
                                                   AudioCache& cache, const GraphBuildOptions& opt)
{
    auto g = std::make_unique<PlaybackGraph>();
    g->sampleRate = opt.sampleRate;
    const double sr = opt.sampleRate;

    bool anySolo = false;
    for (const auto& t : tracks)
        anySolo = anySolo || (bool) t[ids::solo];

    int slot = 0;
    for (const auto& t : tracks)
    {
        if (! t.hasType (ids::TRACK) || slot >= maxTracks)
            continue;
        PlayTrack pt;
        pt.meterSlot = slot++;
        const float gain = juce::Decibels::decibelsToGain ((float) (double) t[ids::gainDb], -96.0f);
        panGains ((float) (double) t[ids::pan], gain, pt.gainL, pt.gainR);
        pt.audible = ! (bool) t[ids::mute] && (! anySolo || (bool) t[ids::solo]);
        pt.monitor = (bool) t[ids::monitor];
        pt.armed = (bool) t[ids::armed];
        pt.inputLeft = (int) t[ids::inputChannel];
        pt.inputRight = (bool) t[ids::inputStereo] ? pt.inputLeft + 1 : -1;

        if (opt.recorders != nullptr)
            if (auto it = opt.recorders->find (t[ids::id].toString()); it != opt.recorders->end())
                pt.recorder = it->second;
        if (opt.rigFor) pt.rig = opt.rigFor (t);
        if (opt.insertsFor) pt.inserts = opt.insertsFor (t);

        // gather clips in layer order with their audio
        std::vector<ClipSpan> spans;
        std::vector<std::shared_ptr<const CachedAudio>> audio;
        for (const auto& c : t)
        {
            if (! c.hasType (ids::CLIP))
                continue;
            const auto f = project::Bundle::fromPortablePath (mediaBundle, c[ids::file].toString());
            std::shared_ptr<const CachedAudio> a;
            if (f != juce::File())
                a = opt.waitForAudio ? cache.loadSync (f) : cache.get (f);
            if (a == nullptr)
            {
                if (! (bool) c[ids::muted]) ++g->missingClips;
                continue;
            }
            ClipSpan s;
            s.start = (double) c[ids::start];
            s.length = (double) c[ids::length];
            s.offset = (double) c[ids::offset];
            s.fadeIn = (double) c[ids::fadeIn];
            s.fadeOut = (double) c[ids::fadeOut];
            s.gain = juce::Decibels::decibelsToGain ((double) c[ids::gainDb], -96.0);
            s.muted = (bool) c[ids::muted];
            s.index = (int) audio.size();
            spans.push_back (s);
            audio.push_back (a);
        }

        for (const auto& p : visiblePieces (spans))
        {
            PlayPiece pp;
            pp.audio = audio[(size_t) p.clip];
            auto toS = [sr] (double sec) { return (juce::int64) std::llround (sec * sr); };
            pp.start = toS (p.start);
            pp.end = toS (p.end);
            if (pp.end <= pp.start)
                continue;
            pp.step = pp.audio->sampleRate / sr;
            pp.srcAtStart = p.srcAtStart * pp.audio->sampleRate;
            pp.gain = (float) p.gain;
            pp.fadeInStart = toS (p.fadeInStart);
            pp.fadeInEnd = toS (p.fadeInEnd);
            pp.fadeOutStart = toS (p.fadeOutStart);
            pp.fadeOutEnd = toS (p.fadeOutEnd);
            pt.pieces.push_back (std::move (pp));
        }
        g->tracks.push_back (std::move (pt));
    }
    return g;
}

namespace
{
    // 4-point, 3rd-order Hermite interpolation
    inline float hermite (const float* d, juce::int64 len, double pos) noexcept
    {
        const auto i = (juce::int64) std::floor (pos);
        const float f = (float) (pos - (double) i);
        auto at = [d, len] (juce::int64 k) { return (k >= 0 && k < len) ? d[k] : 0.0f; };
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
}

void renderTrackClips (const PlayTrack& track, juce::int64 t0, int n, float* left, float* right) noexcept
{
    const juce::int64 t1 = t0 + n;
    for (const auto& p : track.pieces)
    {
        if (p.end <= t0 || p.start >= t1)
            continue;
        const auto& buf = p.audio->buffer;
        const auto len = (juce::int64) buf.getNumSamples();
        const float* srcL = buf.getReadPointer (0);
        const float* srcR = buf.getReadPointer (buf.getNumChannels() > 1 ? 1 : 0);

        const auto a = juce::jmax (t0, p.start), b = juce::jmin (t1, p.end);
        const bool integerStep = juce::exactlyEqual (p.step, 1.0) && juce::exactlyEqual (p.srcAtStart, std::floor (p.srcAtStart));
        const bool fades = a < p.fadeInEnd || b > p.fadeOutStart;

        for (auto t = a; t < b; ++t)
        {
            float g = p.gain;
            if (fades)
                g *= (float) (fadeGain ((double) t, (double) p.fadeInStart, (double) p.fadeInEnd, true)
                              * fadeGain ((double) t, (double) p.fadeOutStart, (double) p.fadeOutEnd, false));
            float l, r;
            if (integerStep)
            {
                const auto k = (juce::int64) p.srcAtStart + (t - p.start);
                if (k < 0 || k >= len) continue;
                l = srcL[k];
                r = srcR[k];
            }
            else
            {
                const double pos = p.srcAtStart + (double) (t - p.start) * p.step;
                if (pos < -1.0 || pos >= (double) len) continue;
                l = hermite (srcL, len, pos);
                r = hermite (srcR, len, pos);
            }
            left[t - t0] += l * g;
            right[t - t0] += r * g;
        }
    }
}
} // namespace ff
