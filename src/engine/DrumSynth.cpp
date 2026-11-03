#include "DrumSynth.h"
#include "core/DrumModel.h"
#include <cmath>

namespace ff
{
using namespace drums;

namespace
{
constexpr double twoPi = juce::MathConstants<double>::twoPi;
// inharmonic oscillator set (classic analogue hi-hat / cymbal ratios)
constexpr double metalFreqs[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };

/** Decay time (seconds to -60 dB / 7) and overall length of each piece. */
double decayOf (int piece)
{
    switch (piece)
    {
        case kick: return 0.30;
        case kick2: return 0.22;
        case snare: return 0.16;
        case sideStick: return 0.035;
        case clap: return 0.13;
        case hhClosed: return 0.045;
        case hhPedal: return 0.03;
        case hhOpen: return 0.42;
        case tomHigh: return 0.32;
        case tomMid: return 0.38;
        case tomLow: return 0.45;
        case tomFloor: return 0.55;
        case crash: return 1.25;
        case crash2: return 1.05;
        case china: return 0.9;
        case splash: return 0.38;
        case ride: return 2.2;
        case rideBell: return 1.2;
        case cowbell: return 0.12;
        default: return 0.2;
    }
}
} // namespace

void DrumSynth::Svf::set (double sampleRate, double fc, double q) noexcept
{
    fc = juce::jlimit (20.0, sampleRate * 0.45, fc);
    g = (float) std::tan (juce::MathConstants<double>::pi * fc / sampleRate);
    k = (float) (1.0 / q);
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

DrumSynth::Svf::Out DrumSynth::Svf::process (float x) noexcept
{
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    return { v2, v1, x - k * v1 - v2 };
}

void DrumSynth::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    for (auto& v : voices) v.active = false;
}

float DrumSynth::noise() noexcept
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (float) (rng & 0xffffff) / 8388608.0f - 1.0f;
}

void DrumSynth::setSample (int piece, const juce::AudioBuffer<float>* sample) noexcept
{
    if (! juce::isPositiveAndBelow (piece, maxPieces)) return;
    for (auto& v : voices)
        if (v.active && v.piece == piece) v.active = false;
    samples[(size_t) piece] = sample;
}

void DrumSynth::allNotesOff() noexcept
{
    for (auto& v : voices)
        if (v.active) v.chokeRate = (float) (1.0 / (0.01 * sr));
}

int DrumSynth::activeVoices() const noexcept
{
    int n = 0;
    for (auto& v : voices) n += v.active ? 1 : 0;
    return n;
}

void DrumSynth::trigger (int piece, float velocity) noexcept
{
    piece = juce::jlimit (0, (int) numPieces - 1, piece);
    const int group = pieceInfo (piece).chokeGroup;
    // hi-hat: any hat hit chokes the ringing ones; any repeated piece cuts its previous voice softly
    for (auto& v : voices)
        if (v.active && ((group != 0 && pieceInfo (v.piece).chokeGroup == group) || v.piece == piece))
            v.chokeRate = (float) (1.0 / ((v.piece == piece && piece >= crash) ? 0.08 * sr : 0.012 * sr));

    Voice* slot = nullptr;
    for (auto& v : voices)
        if (! v.active) { slot = &v; break; }
    if (slot == nullptr)
    {
        slot = &voices[0];
        for (auto& v : voices)
            if (v.order < slot->order) slot = &v;
    }
    auto& v = *slot;
    v = Voice();
    v.active = true;
    v.piece = piece;
    v.vel = juce::jlimit (0.02f, 1.0f, velocity);
    v.order = ++counter;
    v.length = decayOf (piece) * 7.0 + 0.02;
    v.sample = samples[(size_t) piece];
    if (v.sample != nullptr) v.length = (double) v.sample->getNumSamples() / sr + 0.001;
    const float pan = pieceInfo (piece).pan;
    const float a = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    v.gainL = std::cos (a) * 1.41f;
    v.gainR = std::sin (a) * 1.41f;
    for (size_t i = 0; i < v.phase.size(); ++i) v.phase[i] = (double) ((rng >> (i * 4)) & 0xff) / 256.0;

    switch (piece)
    {
        case snare: v.f1.set (sr, 1800.0, 0.7); break;
        case sideStick: v.f1.set (sr, 1900.0, 3.0); break;
        case clap: v.f1.set (sr, 1150.0, 1.4); break;
        case hhClosed: case hhPedal: case hhOpen: v.f1.set (sr, 7200.0, 0.7); v.f2.set (sr, 10500.0, 1.2); break;
        case crash: case crash2: case splash: v.f1.set (sr, 3800.0, 0.7); v.f2.set (sr, 9000.0, 0.6); break;
        case china: v.f1.set (sr, 2400.0, 1.6); break;
        case ride: v.f1.set (sr, 5200.0, 0.7); break;
        case rideBell: v.f1.set (sr, 2500.0, 0.8); break;
        case cowbell: v.f1.set (sr, 2600.0, 2.0); break;
        default: v.f1.set (sr, 4000.0, 0.7); break;
    }
}

float DrumSynth::sample (Voice& v) noexcept
{
    const double t = v.t / sr;
    const double d = decayOf (v.piece);
    const float env = (float) std::exp (-t / d);
    auto sine = [&] (double& ph, double f) { ph += f / sr; if (ph >= 1.0) ph -= 1.0; return (float) std::sin (twoPi * ph); };
    auto metal = [&] (double scale) {
        float s = 0.0f;
        for (size_t i = 0; i < 6; ++i)
        {
            v.phase[i] += metalFreqs[i] * scale / sr;
            if (v.phase[i] >= 1.0) v.phase[i] -= 1.0;
            s += v.phase[i] < 0.5 ? 1.0f : -1.0f;
        }
        return s / 6.0f;
    };

    float out = 0.0f;
    switch (v.piece)
    {
        case kick:
        case kick2:
        {
            const double f = (v.piece == kick ? 46.0 : 54.0) + 115.0 * std::exp (-t / 0.032);
            const float body = sine (v.sinePhase, f) * env;
            const float click = noise() * (float) std::exp (-t / 0.0018) * 0.35f;
            out = std::tanh (1.7f * (body + click)) * 0.95f;
            break;
        }
        case snare:
        {
            const float tone = (sine (v.sinePhase, 186.0) * 0.6f + sine (v.sinePhase2, 334.0) * 0.4f) * (float) std::exp (-t / 0.065);
            const float wires = v.f1.process (noise()).hp * env;
            out = 0.55f * tone + 0.75f * wires;
            break;
        }
        case sideStick:
            out = (v.f1.process (noise()).bp * 1.8f + sine (v.sinePhase, 520.0) * (float) std::exp (-t / 0.012) * 0.5f) * env;
            break;
        case clap:
        {
            float e = 0.0f;
            for (double start : { 0.0, 0.011, 0.023 })
                if (t >= start) e += (float) std::exp (-(t - start) / 0.0055);
            if (t > 0.03) e += (float) std::exp (-(t - 0.03) / d) * 0.55f;
            out = v.f1.process (noise()).bp * e * 1.6f;
            break;
        }
        case hhClosed:
        case hhPedal:
        case hhOpen:
        {
            const float m = metal (1.7) * 0.7f + noise() * 0.3f;
            out = v.f2.process (v.f1.process (m).hp).bp * env * (v.piece == hhPedal ? 1.2f : 2.0f);
            break;
        }
        case tomHigh:
        case tomMid:
        case tomLow:
        case tomFloor:
        {
            const double f0 = v.piece == tomHigh ? 196.0 : v.piece == tomMid ? 156.0 : v.piece == tomLow ? 122.0 : 94.0;
            const double f = f0 * (1.0 + 0.45 * std::exp (-t / 0.05));
            out = std::tanh (1.3f * (sine (v.sinePhase, f) * env + noise() * (float) std::exp (-t / 0.008) * 0.2f));
            break;
        }
        case crash:
        case crash2:
        case splash:
        {
            const double scale = v.piece == splash ? 2.4 : v.piece == crash2 ? 1.55 : 1.35;
            const float m = metal (scale) * 0.55f + noise() * 0.45f;
            out = v.f2.process (v.f1.process (m).hp).lp * env * 1.5f;
            break;
        }
        case china:
        {
            const float m = metal (1.15) * 0.6f + noise() * 0.4f;
            out = std::tanh (3.0f * v.f1.process (m).bp) * env * 0.8f;
            break;
        }
        case ride:
        {
            const float m = metal (2.1) * 0.75f + noise() * 0.12f;
            out = v.f1.process (m).hp * env * 0.9f + sine (v.sinePhase, 3150.0) * (float) std::exp (-t / 0.22) * 0.12f;
            break;
        }
        case rideBell:
        {
            out = (sine (v.sinePhase, 780.0) * 0.5f + sine (v.sinePhase2, 2153.0) * 0.3f) * env
                  + v.f1.process (metal (2.1)).bp * env * 0.3f;
            break;
        }
        case cowbell:
        {
            v.phase[0] += 562.0 / sr; if (v.phase[0] >= 1.0) v.phase[0] -= 1.0;
            v.phase[1] += 845.0 / sr; if (v.phase[1] >= 1.0) v.phase[1] -= 1.0;
            const float sq = (v.phase[0] < 0.5 ? 0.5f : -0.5f) + (v.phase[1] < 0.5 ? 0.5f : -0.5f);
            out = v.f1.process (sq).bp * env * 0.75f;
            break;
        }
        default: break;
    }
    return out * v.vel * v.vel * 0.6f;
}

void DrumSynth::render (float* left, float* right, int n) noexcept
{
    const float vol = juce::jlimit (0.0f, 1.5f, volume);
    constexpr int chunk = 128;
    float bl[chunk], br[chunk];
    for (int start = 0; start < n; start += chunk)
    {
        const int len = juce::jmin (chunk, n - start);
        std::fill (bl, bl + len, 0.0f);
        std::fill (br, br + len, 0.0f);
        for (auto& v : voices)
        {
            if (! v.active) continue;
            if (v.sample != nullptr)
            {
                const auto* buf = v.sample;
                const int total = buf->getNumSamples();
                const float* l = buf->getReadPointer (0);
                const float* r = buf->getReadPointer (buf->getNumChannels() > 1 ? 1 : 0);
                const bool stereo = buf->getNumChannels() > 1;
                const float g = vol * std::pow (v.vel, 1.5f) * 0.9f;
                for (int i = 0; i < len; ++i)
                {
                    if (v.samplePos >= total) { v.active = false; break; }
                    float c = g;
                    if (v.chokeRate > 0.0f)
                    {
                        v.choke -= v.chokeRate;
                        if (v.choke <= 0.0f) { v.active = false; break; }
                        c *= v.choke;
                    }
                    if (stereo) { bl[i] += l[v.samplePos] * c; br[i] += r[v.samplePos] * c; }
                    else { bl[i] += l[v.samplePos] * c * v.gainL; br[i] += l[v.samplePos] * c * v.gainR; }
                    ++v.samplePos;
                }
                continue;
            }
            for (int i = 0; i < len; ++i)
            {
                float s = sample (v) * vol;
                if (v.chokeRate > 0.0f)
                {
                    v.choke -= v.chokeRate;
                    if (v.choke <= 0.0f) { v.active = false; break; }
                    s *= v.choke;
                }
                bl[i] += s * v.gainL;
                br[i] += s * v.gainR;
                v.t += 1.0;
                if (v.t / sr > v.length) { v.active = false; break; }
            }
        }
        // soft knee above -2 dB so stacked hits (blasts, crash + kick) never clip hard
        auto knee = [] (float x) {
            const float a = std::abs (x);
            if (a <= 0.8f) return x;
            return std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f), x);
        };
        for (int i = 0; i < len; ++i)
        {
            left[start + i] += knee (bl[i]);
            right[start + i] += knee (br[i]);
        }
    }
}

void DrumSynth::renderOffline (const std::vector<DrumHit>& hits, double sampleRate, double lengthSec, float volume,
                               juce::AudioBuffer<float>& out, const SampleSet* sampleSet)
{
    const int total = (int) std::ceil (juce::jmax (0.0, lengthSec) * sampleRate);
    out.setSize (2, total);
    out.clear();
    DrumSynth s;
    s.prepare (sampleRate);
    s.volume = volume;
    if (sampleSet != nullptr)
        for (int i = 0; i < maxPieces; ++i) s.setSample (i, (*sampleSet)[(size_t) i]);
    size_t next = 0;
    int pos = 0;
    while (pos < total)
    {
        int end = juce::jmin (total, pos + 256);
        if (next < hits.size())
            end = juce::jmin (end, juce::jmax (pos, (int) std::llround (hits[next].sec * sampleRate)));
        if (end > pos)
            s.render (out.getWritePointer (0, pos), out.getWritePointer (1, pos), end - pos);
        pos = end;
        while (next < hits.size() && (int) std::llround (hits[next].sec * sampleRate) <= pos)
        {
            s.trigger (hits[next].piece, hits[next].velocity);
            ++next;
        }
    }
}
} // namespace ff
