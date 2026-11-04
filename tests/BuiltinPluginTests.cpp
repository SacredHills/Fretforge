#include "engine/fx/BuiltinPlugins.h"
#include <cmath>

using namespace ff;
namespace bi = ff::fx::builtin;

namespace
{
constexpr double sr = 48000.0;

void setP (juce::AudioPluginInstance& p, const juce::String& id, float v)
{
    for (auto* prm : p.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm); r != nullptr && r->getParameterID() == id)
            r->setValueNotifyingHost (r->convertTo0to1 (v));
}

float getP (juce::AudioPluginInstance& p, const juce::String& id)
{
    for (auto* prm : p.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm); r != nullptr && r->getParameterID() == id)
            return r->convertFrom0to1 (r->getValue());
    return -999.0f;
}

/** Runs a stereo signal through the plugin in 256-sample blocks. */
juce::AudioBuffer<float> run (juce::AudioPluginInstance& p, const juce::AudioBuffer<float>& in)
{
    juce::AudioBuffer<float> out (in);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < out.getNumSamples(); pos += 256)
    {
        const int n = juce::jmin (256, out.getNumSamples() - pos);
        float* ch[2] = { out.getWritePointer (0, pos), out.getWritePointer (1, pos) };
        juce::AudioBuffer<float> view (ch, 2, n);
        p.processBlock (view, midi);
    }
    return out;
}

juce::AudioBuffer<float> sine (double freq, float amp, int n)
{
    juce::AudioBuffer<float> b (2, n);
    for (int i = 0; i < n; ++i)
    {
        const float v = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / sr);
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    return b;
}

float peakDb (const juce::AudioBuffer<float>& b, int from)
{
    return juce::Decibels::gainToDecibels (b.getMagnitude (0, from, b.getNumSamples() - from), -120.0f);
}

std::unique_ptr<juce::AudioPluginInstance> make (const char* id)
{
    auto p = bi::create (id);
    p->setRateAndBufferSizeDetails (sr, 256);
    p->prepareToPlay (sr, 256);
    return p;
}
}

class BuiltinPluginTests final : public juce::UnitTest
{
public:
    BuiltinPluginTests() : juce::UnitTest ("Built-in plugins", "Engine") {}

    void runTest() override
    {
        beginTest ("every built-in loads, has a description and stays finite on noise");
        for (auto& info : bi::all())
        {
            auto p = make (info.id);
            expect (p != nullptr, info.id);
            juce::PluginDescription d;
            p->fillInPluginDescription (d);
            expectEquals (d.pluginFormatName, juce::String (bi::formatName));
            expect (bi::descriptionXml (info.id).contains (info.id));
            juce::AudioBuffer<float> noise (2, 48000);
            juce::Random rnd (1);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 48000; ++i) noise.setSample (c, i, rnd.nextFloat() * 2.0f - 1.0f);
            auto out = run (*p, noise);
            bool finite = true;
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 48000; ++i) finite = finite && std::isfinite (out.getSample (c, i));
            expect (finite, info.id);
            expect (out.getMagnitude (0, 48000) < 8.0f, info.id);
        }

        beginTest ("state round-trip and stateWith");
        {
            auto st = bi::stateWith ("fretforge:reverb", { { "mix", 100.0f }, { "decay", 4.0f } });
            juce::MemoryBlock mb;
            expect (mb.fromBase64Encoding (st));
            auto p = make ("fretforge:reverb");
            p->setStateInformation (mb.getData(), (int) mb.getSize());
            expectWithinAbsoluteError (getP (*p, "mix"), 100.0f, 0.01f);
            expectWithinAbsoluteError (getP (*p, "decay"), 4.0f, 0.01f);
        }

        beginTest ("EQ: bell boost and low cut");
        {
            auto p = make ("fretforge:eq");
            expectWithinAbsoluteError (peakDb (run (*p, sine (1000.0, 0.25f, 24000)), 12000), -12.04f, 0.1f);   // flat by default
            setP (*p, "m1F", 1000.0f);
            setP (*p, "m1G", 12.0f);
            setP (*p, "m1Q", 1.0f);
            expectWithinAbsoluteError (peakDb (run (*p, sine (1000.0, 0.25f, 24000)), 12000), -12.04f + 12.0f, 0.3f);
            setP (*p, "m1G", 0.0f);
            setP (*p, "hp", 500.0f);
            expect (peakDb (run (*p, sine (50.0, 0.25f, 48000)), 24000) < -12.04f - 30.0f, "50 Hz is cut");
        }

        beginTest ("compressor: 4:1 above the threshold");
        {
            auto p = make ("fretforge:comp");
            setP (*p, "threshold", -20.0f);
            setP (*p, "ratio", 4.0f);
            setP (*p, "knee", 0.0f);
            setP (*p, "attack", 1.0f);
            setP (*p, "release", 300.0f);
            const float out = peakDb (run (*p, sine (200.0, 1.0f, 48000)), 24000);
            expectWithinAbsoluteError (out, -15.0f, 1.5f);   // 0 dB in -> -20 + 20/4
            setP (*p, "mix", 0.0f);
            expectWithinAbsoluteError (peakDb (run (*p, sine (200.0, 1.0f, 24000)), 12000), 0.0f, 0.05f);
        }

        beginTest ("delay: repeats at the set time");
        {
            auto p = make ("fretforge:delay");
            setP (*p, "time", 100.0f);
            setP (*p, "feedback", 0.0f);
            setP (*p, "mix", 100.0f);
            setP (*p, "lowCut", 20.0f);
            setP (*p, "highCut", 20000.0f);
            p->prepareToPlay (sr, 256);
            juce::AudioBuffer<float> imp (2, 12000);
            imp.clear();
            imp.setSample (0, 100, 1.0f);
            imp.setSample (1, 100, 1.0f);
            auto out = run (*p, imp);
            int at = 0;
            float best = 0.0f;
            for (int i = 0; i < 12000; ++i) if (std::abs (out.getSample (0, i)) > best) { best = std::abs (out.getSample (0, i)); at = i; }
            expect (std::abs (at - (100 + 4800)) <= 3, "echo at " + juce::String (at));
        }

        beginTest ("limiter: nothing passes the ceiling, quiet audio is untouched (only delayed)");
        {
            auto p = make ("fretforge:limiter");
            setP (*p, "ceiling", -1.0f);
            setP (*p, "gain", 12.0f);
            p->prepareToPlay (sr, 256);
            expect (p->getLatencySamples() > 0 && p->getLatencySamples() < 200);
            juce::AudioBuffer<float> loud (2, 48000);
            juce::Random rnd (3);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 48000; ++i) loud.setSample (c, i, (rnd.nextFloat() * 2.0f - 1.0f) * (i % 9000 < 300 ? 1.0f : 0.3f));
            auto out = run (*p, loud);
            expect (out.getMagnitude (0, 48000) <= juce::Decibels::decibelsToGain (-1.0f) + 1.0e-6f, "peak " + juce::String (out.getMagnitude (0, 48000)));
            setP (*p, "gain", 0.0f);
            p->prepareToPlay (sr, 256);
            auto quiet = sine (440.0, 0.25f, 24000);
            auto q = run (*p, quiet);
            const int lat = p->getLatencySamples();
            float maxErr = 0.0f;
            for (int i = 12000; i < 24000; ++i) maxErr = juce::jmax (maxErr, std::abs (q.getSample (0, i) - quiet.getSample (0, i - lat)));
            expect (maxErr < 1.0e-3f, "quiet audio changed by " + juce::String (maxErr));
        }

        beginTest ("reverb: decays, and mix 0 is dry");
        {
            auto p = make ("fretforge:reverb");
            setP (*p, "mix", 100.0f);
            setP (*p, "decay", 1.0f);
            setP (*p, "predelay", 0.0f);
            juce::AudioBuffer<float> imp (2, 48000 * 3);
            imp.clear();
            imp.setSample (0, 0, 1.0f);
            imp.setSample (1, 0, 1.0f);
            auto out = run (*p, imp);
            const float early = out.getRMSLevel (0, 4800, 4800), late = out.getRMSLevel (0, 48000 * 2, 24000);
            expect (early > 1.0e-4f, "there is a reverb tail");
            expect (late < early * 0.01f, "1 s decay: down by > 40 dB two seconds later");
            setP (*p, "mix", 0.0f);
            expectWithinAbsoluteError (peakDb (run (*p, sine (440.0, 0.5f, 12000)), 6000), -6.02f, 0.05f);
        }
    }
};

static BuiltinPluginTests builtinPluginTests;
