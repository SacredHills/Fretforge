#include "TabSynth.h"
#include "core/TabModel.h"
#include "core/DrumModel.h"
#include "engine/PlaybackGraph.h"
#include <cmath>

namespace ff
{
std::unique_ptr<TabSequence> buildTabSequence (const juce::ValueTree& score, double speed)
{
    auto seq = std::make_unique<TabSequence>();
    if (! score.isValid())
        return seq;
    const auto map = tab::tempoMap (score, speed);
    seq->secondsPerQuarter = map.segments.front().secondsPerQ;

    // per-track mix (mute / solo / volume / pan)
    struct Mix { float l, r; bool audible, bass; };
    std::vector<Mix> mix;
    bool anySolo = false;
    for (auto t : tab::tracks (score)) anySolo = anySolo || (bool) t[tab::ids::solo];
    for (auto t : tab::tracks (score))
    {
        Mix m;
        panGains ((float) (double) t[tab::ids::pan], (float) (double) t.getProperty (tab::ids::volume, 0.8), m.l, m.r);
        m.audible = ! (bool) t[tab::ids::mute] && (! anySolo || (bool) t[tab::ids::solo]);
        m.bass = t[tab::ids::instrument].toString() == "bass";
        mix.push_back (m);
    }

    for (const auto& e : tab::compile (score))
    {
        if (! juce::isPositiveAndBelow (e.track, (int) mix.size()) || ! mix[(size_t) e.track].audible)
            continue;
        SynthNote n;
        n.startSec = map.secondsAt (e.startQ);
        n.endSec = map.secondsAt (e.startQ + juce::jmax (0.02, e.lengthQ));
        n.track = e.track;
        n.string = e.string;
        n.frequency = (float) (440.0 * std::pow (2.0, (e.midi - 69) / 12.0));
        n.velocity = e.velocity;
        n.bend = e.bend;
        n.prebend = e.prebend;
        n.vibrato = e.vibrato;
        n.palmMute = e.palmMute;
        n.dead = e.dead;
        n.legato = e.legato;
        n.harmonic = e.harmonic;
        n.slideTo = e.slideToSemis;
        n.tremolo = e.tremolo;
        n.tremoloInterval = (float) (60.0 / map.bpmAt (e.startQ) / 8.0); // 32nd notes
        n.whammy = e.whammy;
        n.bass = mix[(size_t) e.track].bass;
        n.gainL = mix[(size_t) e.track].l;
        n.gainR = mix[(size_t) e.track].r;
        seq->notes.push_back (n);
        seq->lengthSec = juce::jmax (seq->lengthSec, n.endSec);
    }
    const auto starts = tab::measureStarts (score);
    seq->lengthSec = juce::jmax (seq->lengthSec, map.secondsAt (starts.back()));

    auto ms = tab::measures (score);
    for (int m = 0; m < ms.getNumChildren(); ++m)
    {
        const int num = juce::jlimit (1, 32, (int) ms.getChild (m).getProperty (tab::ids::tsNum, 4));
        const double beatQ = 4.0 / juce::jlimit (1, 64, (int) ms.getChild (m).getProperty (tab::ids::tsDen, 4));
        for (int b = 0; b < num; ++b)
            seq->clicks.push_back ({ map.secondsAt (starts[(size_t) m] + b * beatQ), b == 0 });
    }
    return seq;
}

std::unique_ptr<TabSequence> buildDrumSequence (const juce::ValueTree& d, const juce::ValueTree& pattern, double bpm)
{
    auto seq = std::make_unique<TabSequence>();
    const double spq = 60.0 / juce::jlimit (10.0, 999.0, bpm);
    seq->secondsPerQuarter = spq;
    seq->drumVolume = (float) (double) d.getProperty (drums::ids::volume, 0.8);
    const auto c = drums::compile (d, pattern, 1);
    for (auto& e : c.events)
        seq->drumHits.push_back ({ juce::jmax (0.0, e.q * spq + e.offsetSec), e.piece, e.velocity });
    std::stable_sort (seq->drumHits.begin(), seq->drumHits.end(), [] (const DrumHit& a, const DrumHit& b) { return a.sec < b.sec; });
    for (auto& b : c.bars)
    {
        const double beatQ = 4.0 / juce::jmax (1, b.den);
        for (int i = 0; i < b.num; ++i)
            seq->clicks.push_back ({ (b.q + i * beatQ) * spq, i == 0 });
    }
    seq->lengthSec = c.lengthQ * spq;
    return seq;
}

// ------------------------------------------------------------------ synth

void GuitarSynth::prepare (double sampleRate, int maxVoices)
{
    sr = sampleRate;
    const int size = (int) (sr / 20.0) + 8; // lowest pitch ~20 Hz (bends below a low B)
    storage.assign ((size_t) (size * maxVoices), 0.0f);
    voices.assign ((size_t) maxVoices, Voice {});
    for (int i = 0; i < maxVoices; ++i)
    {
        voices[(size_t) i].line = storage.data() + (size_t) i * (size_t) size;
        voices[(size_t) i].lineSize = size;
    }
}

void GuitarSynth::allNotesOff() noexcept
{
    for (auto& v : voices)
        if (v.active) { v.released = true; v.releaseGain = 0.995f; }
}

int GuitarSynth::activeVoices() const noexcept
{
    int n = 0;
    for (auto& v : voices) n += v.active ? 1 : 0;
    return n;
}

float GuitarSynth::noise() noexcept
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (float) ((double) (rng & 0xFFFFFF) / (double) 0x800000 - 1.0);
}

double GuitarSynth::currentSemis (const Voice& v) const noexcept
{
    const auto& n = v.note;
    const double len = juce::jmax (0.02, n.endSec - n.startSec);
    double s = 0.0;
    if (n.prebend) s += n.bend;
    else if (n.bend > 0.0f)
    {
        const double riseTime = juce::jlimit (0.06, 0.3, len * 0.3);
        s += n.bend * juce::jmin (1.0, v.age / riseTime);
    }
    if (n.vibrato && v.age > 0.08)
        s += (n.bass ? 0.15 : 0.25) * std::sin (juce::MathConstants<double>::twoPi * 5.5 * (v.age - 0.08));
    if (std::abs (n.slideTo) > 0.0f)
    {
        const double from = len * 0.7;
        if (v.age > from) s += n.slideTo * juce::jmin (1.0, (v.age - from) / (len * 0.3));
    }
    if (std::abs (n.whammy) > 0.0f)
        s += n.whammy * juce::jmin (1.0, v.age / len);
    return s;
}

void GuitarSynth::excite (Voice& v, float amount) noexcept
{
    const auto& n = v.note;
    const double period = sr / juce::jmax (20.0, (double) n.frequency);
    const int len = juce::jlimit (2, v.lineSize - 2, (int) period);
    // pluck: filtered noise, softer for legato / palm mute, plus a sine for harmonics
    float lpState = 0.0f;
    const float pickLp = n.legato ? 0.25f : (n.palmMute ? 0.35f : (n.bass ? 0.4f : 0.65f));
    for (int i = 0; i < len; ++i)
    {
        lpState += pickLp * (noise() - lpState);
        float x = lpState;
        if (n.harmonic != 0)
            x = 0.25f * x + 0.9f * (float) std::sin (juce::MathConstants<double>::twoPi * i / period);
        const int idx = (v.writePos - len + i + v.lineSize) % v.lineSize;
        v.line[idx] = v.line[idx] * (amount < 1.0f ? 0.6f : 0.0f) + x * amount;
    }
}

void GuitarSynth::start (const SynthNote& n) noexcept
{
    if (voices.empty())
        return;

    // A string can only sound once: re-plucking (or a legato note) takes over the old voice.
    Voice* v = nullptr;
    for (auto& x : voices)
        if (x.active && x.note.track == n.track && x.note.string == n.string)
        {
            v = &x;
            break;
        }
    const bool continuing = v != nullptr && n.legato;
    if (v == nullptr)
    {
        for (auto& x : voices)
            if (! x.active) { v = &x; break; }
        if (v == nullptr) // steal the oldest
        {
            v = &voices[0];
            for (auto& x : voices)
                if (x.order < v->order) v = &x;
        }
    }

    v->note = n;
    v->active = true;
    v->released = false;
    v->age = 0.0;
    v->order = ++counter;
    v->nextRepick = n.tremoloInterval;

    const double f = juce::jmax (20.0, (double) n.frequency);
    double t60 = n.bass ? 4.5 : juce::jmap (juce::jlimit (80.0, 1000.0, f), 80.0, 1000.0, 4.0, 1.4);
    float bright = n.bass ? 0.42f : 0.18f;
    if (n.palmMute) { t60 = 0.22; bright = 0.48f; }
    if (n.dead) { t60 = 0.035; bright = 0.5f; }
    if (n.harmonic != 0) { t60 *= 0.8; bright = 0.12f; }
    v->decayGain = (float) std::pow (10.0, -3.0 / (t60 * f));
    v->releaseGain = (float) std::pow (10.0, -3.0 / (0.07 * f));
    v->brightness = bright;

    if (! continuing)
    {
        std::fill (v->line, v->line + v->lineSize, 0.0f);
        v->prev = v->lp = 0.0f;
        v->writePos = 0;
    }
    excite (*v, continuing ? 0.45f * n.velocity : n.velocity * (n.dead ? 0.5f : 1.0f));
    v->envelope = 1.0f;
}

void GuitarSynth::render (float* left, float* right, int numSamples) noexcept
{
    const double dt = 1.0 / sr;
    for (auto& v : voices)
    {
        if (! v.active) continue;
        const auto& n = v.note;
        double semis = currentSemis (v);
        double freq = n.frequency * std::pow (2.0, semis / 12.0);
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            if ((i & 31) == 0 && i > 0)
            {
                semis = currentSemis (v);
                freq = n.frequency * std::pow (2.0, semis / 12.0);
            }
            if (! v.released && v.age >= n.endSec - n.startSec)
                v.released = true;
            if (n.tremolo && ! v.released && v.age >= v.nextRepick)
            {
                excite (v, 0.5f * n.velocity);
                v.nextRepick += n.tremoloInterval;
            }

            const double delay = juce::jlimit (2.0, (double) v.lineSize - 3.0, sr / freq - v.brightness);
            double rp = v.writePos - delay;
            while (rp < 0) rp += v.lineSize;
            const int i0 = (int) rp;
            const float frac = (float) (rp - i0);
            const float a = v.line[i0 % v.lineSize], b = v.line[(i0 + 1) % v.lineSize];
            const float out = a + (b - a) * frac;

            const float g = v.released ? v.releaseGain : v.decayGain;
            const float filtered = g * ((1.0f - v.brightness) * out + v.brightness * v.prev);
            v.prev = out;
            v.line[v.writePos] = filtered;
            v.writePos = (v.writePos + 1) % v.lineSize;

            // gentle output low-pass gives a rounder, less "plinky" tone
            v.lp += (n.bass ? 0.35f : 0.6f) * (out - v.lp);
            const float s = v.lp * 0.5f;
            left[i] += s * n.gainL;
            right[i] += s * n.gainR;
            peak = juce::jmax (peak, std::abs (s));
            v.age += dt;
        }
        v.envelope = peak;
        if ((v.released && peak < 1.0e-4f) || (peak < 1.0e-6f && v.age > 0.05))
            v.active = false;
    }
}
} // namespace ff
