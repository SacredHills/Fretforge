#include <juce_audio_formats/juce_audio_formats.h>
#include "core/RigModel.h"
#include "core/ProjectFile.h"
#include "core/Timeline.h"
#include "engine/fx/AmpRig.h"
#include "engine/fx/NamModel.h"
#include "engine/fx/RigBank.h"
#include "engine/AudioEngine.h"
#include "engine/Exporter.h"
#include "NAM/get_dsp.h"
#include "NAM/dsp.h"
#include <cmath>

using namespace ff;
namespace rg = ff::rig;
namespace tl = ff::timeline;

namespace
{
juce::File namData (const juce::String& name) { return juce::File (FF_TEST_DATA_DIR).getChildFile ("nam").getChildFile (name); }

juce::File tempFile (const juce::String& name)
{
    auto d = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ff_rig_tests");
    d.createDirectory();
    return d.getChildFile (name);
}

/** A NAM "Linear" model: y[n] = sum w[k] x[n-k]. weights = {1} is the identity. */
juce::File writeLinearModel (const juce::String& name, std::vector<float> weights, double sr = 48000.0)
{
    juce::String w;
    for (size_t i = 0; i < weights.size(); ++i) w << (i ? "," : "") << juce::String (weights[i], 6);
    auto f = tempFile (name);
    f.replaceWithText ("{\"version\":\"0.5.4\",\"architecture\":\"Linear\",\"config\":{\"receptive_field\":" + juce::String ((int) weights.size())
                       + ",\"bias\":false},\"weights\":[" + w + "],\"sample_rate\":" + juce::String (sr, 1)
                       + ",\"metadata\":{\"name\":\"Identity\",\"loudness\":-18.0,\"input_level_dbu\":6.0}}");
    return f;
}

/** Rig with every block switched off (output = mono sum of the input). */
void allOff (fx::AmpRig& r)
{
    for (int b = 0; b < rg::numBlocks; ++b) r.setParam (rg::switchFor (b), 0.0f);
}

float peakOf (const std::vector<float>& v, size_t from = 0, size_t to = SIZE_MAX)
{
    float p = 0;
    for (size_t i = from; i < juce::jmin (to, v.size()); ++i) p = juce::jmax (p, std::abs (v[i]));
    return p;
}

bool allFinite (const std::vector<float>& v)
{
    for (float x : v) if (! std::isfinite (x)) return false;
    return true;
}

/** Runs `in` (mono) through the rig in blocks; returns left output. */
std::vector<float> runRig (fx::AmpRig& r, const std::vector<float>& in, int block, std::vector<float>* right = nullptr)
{
    std::vector<float> out (in.size()), outR (in.size());
    for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
    {
        const int n = (int) juce::jmin ((size_t) block, in.size() - pos);
        std::copy (in.begin() + (long) pos, in.begin() + (long) pos + n, out.begin() + (long) pos);
        std::copy (in.begin() + (long) pos, in.begin() + (long) pos + n, outR.begin() + (long) pos);
        r.process (out.data() + pos, outR.data() + pos, n);
    }
    if (right) *right = outR;
    return out;
}

std::vector<float> sine (double freq, double sr, int n, float amp = 0.5f)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i) v[(size_t) i] = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / sr);
    return v;
}

/** Best lag (0..maxLag) of b against a, and the residual (dB relative to a) at that lag. */
std::pair<int, double> bestLag (const std::vector<float>& a, const std::vector<float>& b, int from, int len, int maxLag)
{
    int best = 0;
    double bestErr = 1e300, ref = 0;
    for (int i = from; i < from + len; ++i) ref += (double) a[(size_t) i] * a[(size_t) i];
    for (int lag = 0; lag <= maxLag; ++lag)
    {
        double e = 0;
        for (int i = from; i < from + len; ++i)
        {
            const double d = b[(size_t) (i + lag)] - a[(size_t) i];
            e += d * d;
        }
        if (e < bestErr) { bestErr = e; best = lag; }
    }
    return { best, 10.0 * std::log10 (juce::jmax (1e-30, bestErr) / juce::jmax (1e-30, ref)) };
}
}

struct RigModelTests : juce::UnitTest
{
    RigModelTests() : juce::UnitTest ("Rig model", "fx") {}

    void runTest() override
    {
        beginTest ("defaults, clamping and switches");
        {
            auto r = rg::makeDefault();
            expect (rg::isEnabled (r));
            for (int p = 0; p < rg::numParams; ++p)
                expectEquals (rg::get (r, p), rg::info (p).def);
            rg::set (r, rg::bass, 40.0f);
            expectEquals (rg::get (r, rg::bass), 12.0f);
            rg::set (r, rg::delayOn, 0.7f);
            expectEquals (rg::get (r, rg::delayOn), 1.0f);
            expectEquals (rg::formatValue (rg::delayTime, 380.4f), juce::String ("380 ms"));
            expectEquals (rg::formatValue (rg::cabHighCut, 12000.0f), juce::String ("12.0 kHz"));
            expectEquals (rg::formatValue (rg::calLevel, 12.0f), juce::String ("12.0 dBu"));
            expect (rg::isToggle (rg::namCalibrate) && ! rg::isToggle (rg::calLevel));
            for (int b = 0; b < rg::numBlocks; ++b)
                expectEquals (rg::info (rg::switchFor (b)).block, b);
        }

        beginTest ("block order: parse, repair, move, pack");
        {
            auto r = rg::makeDefault();
            expect (rg::getOrder (r) == rg::defaultOrder());
            r.setProperty (rg::ids::order, "reverb,amp,bogus,amp,gate", nullptr);
            auto o = rg::getOrder (r);
            expectEquals (o[0], (int) rg::reverb);
            expectEquals (o[1], (int) rg::amp);
            expectEquals (o[2], (int) rg::gate);
            std::set<int> all (o.begin(), o.end());
            expectEquals ((int) all.size(), (int) rg::numBlocks);   // missing blocks appended, no duplicates
            rg::moveBlock (r, rg::amp, -1);
            expectEquals (rg::getOrder (r)[0], (int) rg::amp);
            rg::moveBlock (r, rg::amp, -1);                        // already first: no change
            expectEquals (rg::getOrder (r)[0], (int) rg::amp);
            expect (rg::unpackOrder (rg::packOrder (rg::getOrder (r))) == rg::getOrder (r));
        }

        beginTest ("presets round-trip and report missing files");
        {
            auto a = rg::makeDefault();
            rg::set (a, rg::driveOn, 1);
            rg::set (a, rg::delayTime, 512);
            a.setProperty (rg::ids::order, "amp,gate,comp,drive,eq,cab,chorus,delay,reverb", nullptr);
            a.setProperty (rg::ids::namFile, "/definitely/not/here.nam", nullptr);
            const auto json = juce::JSON::toString (rg::toPreset (a, "Mine"));
            auto b = rg::makeDefault();
            const auto missing = rg::applyPreset (b, juce::JSON::parse (json));
            expectEquals (missing.size(), 1);
            expectEquals (missing[0], juce::String ("here.nam"));
            expectEquals (rg::get (b, rg::driveOn), 1.0f);
            expectEquals (rg::get (b, rg::delayTime), 512.0f);
            expectEquals (rg::getOrder (b)[0], (int) rg::amp);
            expectEquals (b[rg::ids::namFile].toString(), juce::String());
            expectEquals (b[rg::ids::rigName].toString(), juce::String ("Mine"));

            const auto f = rg::factoryPresets();
            expect (f.size() >= 4);
            for (auto& p : f)
            {
                auto t = rg::makeDefault();
                t.setProperty (rg::ids::namFile, "/my/amp.nam", nullptr);
                expect (rg::applyPreset (t, p.json).isEmpty(), p.name);
                expectEquals (t[rg::ids::namFile].toString(), juce::String ("/my/amp.nam"));   // kept
            }
        }
    }
};

struct NamModelTests : juce::UnitTest
{
    NamModelTests() : juce::UnitTest ("NAM model", "fx") {}

    void runTest() override
    {
        beginTest ("metadata");
        {
            fx::NamInfo info;
            juce::String err;
            expect (fx::readNamInfo (namData ("wavenet.nam"), info, err), err);
            expectEquals (info.architecture, juce::String ("WaveNet"));
            expectEquals (info.gearMake, juce::String ("Darkglass Electronics"));
            expectEquals (info.sampleRate, 48000.0);
            expect (info.hasLoudness);
            expect (info.displayName().contains ("Microtubes"));

            expect (! fx::readNamInfo (tempFile ("missing.nam"), info, err));
            auto junk = tempFile ("junk.nam");
            junk.replaceWithText ("{ not json");
            expect (! fx::readNamInfo (junk, info, err));
            expect (err.contains ("not a valid"));
            auto notNam = tempFile ("other.nam");
            notNam.replaceWithText ("{\"hello\": 1}");
            expect (fx::NamModel::load (notNam, err) == nullptr);
        }

        beginTest ("our wrapper is bit-identical to NAM itself at the model's rate");
        for (auto name : { "wavenet.nam", "lstm.nam", "wavenet_a1_standard.nam" })
        {
            juce::String err;
            auto m = fx::NamModel::load (namData (name), err);
            expect (m != nullptr, err);
            if (m == nullptr) continue;
            m->prepare (48000.0, 256);
            expect (! m->isResampling());

            auto ref = nam::get_dsp (std::filesystem::path (namData (name).getFullPathName().toStdString()));
            ref->Reset (48000.0, 4096);

            auto in = sine (220.0, 48000.0, 4800, 0.3f);
            std::vector<float> ours = in, theirs (in.size());
            for (size_t p = 0; p < in.size(); p += 256)
            {
                const int n = (int) juce::jmin ((size_t) 256, in.size() - p);
                m->process (ours.data() + p, n);
                float* ip[1] = { in.data() + p };
                float* op[1] = { theirs.data() + p };
                ref->process (ip, op, n);
            }
            expect (ours == theirs, name);
            expect (allFinite (ours) && peakOf (ours, 2400) > 1.0e-4f, name);
        }

        beginTest ("resampling: identity model at 44.1 and 96 kHz");
        for (double devRate : { 44100.0, 96000.0, 32000.0 })
        {
            juce::String err;
            auto m = fx::NamModel::load (writeLinearModel ("identity.nam", { 1.0f }), err);
            expect (m != nullptr, err);
            m->prepare (devRate, 512);
            expect (m->isResampling());
            const int n = (int) devRate;   // 1 s
            // band-limited noise: unlike a sine, it has only one alignment that matches
            std::vector<float> in ((size_t) n);
            juce::Random rnd (7);
            fx::Biquad lp1, lp2;
            lp1.set (fx::Biquad::lowPass, devRate, 3000.0, 0.541);
            lp2.set (fx::Biquad::lowPass, devRate, 3000.0, 1.307);
            for (auto& x : in) x = lp2.process (lp1.process (rnd.nextFloat() - 0.5f));
            auto out = in;
            for (int p = 0; p < n; p += 333)                     // odd block sizes on purpose
                m->process (out.data() + p, juce::jmin (333, n - p));
            const auto [lag, errDb] = bestLag (in, out, n / 4, n / 2, 200);
            juce::ignoreUnused (errDb);
            expect (std::abs (lag - m->latencySamples()) <= 1, "latency: measured " + juce::String (lag) + ", reported " + juce::String (m->latencySamples()));

            // quality: a 440 Hz tone comes out as the same tone (no pitch change, nothing added)
            m->reset();
            auto tone = sine (440.0, devRate, n, 0.5f);
            for (int p = 0; p < n; p += 333) m->process (tone.data() + p, juce::jmin (333, n - p));
            double ss = 0, sc = 0, s2 = 0, c2 = 0, sx = 0, tot = 0;
            for (int i = n / 4; i < n * 3 / 4; ++i)
            {
                const double ph = 2.0 * juce::MathConstants<double>::pi * 440.0 * i / devRate;
                const double x = tone[(size_t) i];
                ss += x * std::sin (ph); sc += x * std::cos (ph);
                s2 += std::sin (ph) * std::sin (ph); c2 += std::cos (ph) * std::cos (ph);
                juce::ignoreUnused (sx);
                tot += x * x;
            }
            const double a = ss / s2, b = sc / c2;
            const double fund = a * a * s2 + b * b * c2;
            const double residualDb = 10.0 * std::log10 (juce::jmax (1e-30, tot - fund) / tot);
            const double amp = std::sqrt (a * a + b * b);
            logMessage ("  " + juce::String (devRate) + " Hz: latency " + juce::String (lag) + " samples, 440 Hz amplitude "
                        + juce::String (amp, 4) + ", residual " + juce::String (residualDb, 1) + " dB");
            expectWithinAbsoluteError (amp, 0.5, 0.005);
            expect (residualDb < -50.0, "resampling adds too much: " + juce::String (residualDb, 1) + " dB");
        }

        beginTest ("normalisation gain from loudness");
        {
            juce::String err;
            auto m = fx::NamModel::load (namData ("wavenet.nam"), err);
            expectWithinAbsoluteError (juce::Decibels::gainToDecibels (m->normalisationGain()), (float) (-18.0 - m->info().loudness), 0.01f);
        }
    }
};

struct AmpRigTests : juce::UnitTest
{
    AmpRigTests() : juce::UnitTest ("Amp rig", "fx") {}

    void runTest() override
    {
        const double sr = 48000.0;

        beginTest ("all blocks off: output is the input (mono sum)");
        {
            fx::AmpRig r;
            r.prepare (sr, 512);
            allOff (r);
            auto in = sine (330.0, sr, 4800);
            auto out = runRig (r, in, 256);
            float maxDiff = 0;
            for (size_t i = 0; i < in.size(); ++i) maxDiff = juce::jmax (maxDiff, std::abs (out[i] - in[i]));
            expectLessThan (maxDiff, 1.0e-6f);
        }

        beginTest ("output gain");
        {
            fx::AmpRig r;
            r.prepare (sr, 512);
            allOff (r);
            r.setParam (rg::outGain, -6.0206f);
            r.reset();
            auto out = runRig (r, sine (330.0, sr, 9600), 512);
            expectWithinAbsoluteError (peakOf (out, 4800), 0.25f, 0.002f);
        }

        beginTest ("noise gate closes on hiss and opens on playing");
        {
            fx::AmpRig r;
            r.prepare (sr, 512);
            allOff (r);
            r.setParam (rg::gateOn, 1);
            r.setParam (rg::gateThresh, -50.0f);
            juce::Random rnd (1);
            std::vector<float> hiss (24000);
            for (auto& x : hiss) x = (rnd.nextFloat() - 0.5f) * 0.002f;   // about -60 dBFS
            auto out = runRig (r, hiss, 256);
            expectLessThan (peakOf (out, 12000), 1.0e-5f);
            expect (! r.gateOpen.load());
            auto loud = runRig (r, sine (200.0, sr, 9600, 0.3f), 256);
            expectGreaterThan (peakOf (loud, 4800), 0.29f);
            expect (r.gateOpen.load());
        }

        beginTest ("built-in amp, drive, eq, cab: bounded, finite, louder harmonics with more gain");
        {
            auto thd = [&] (float gain) {
                fx::AmpRig r;
                r.prepare (sr, 512);
                allOff (r);
                r.setParam (rg::ampOn, 1);
                r.setParam (rg::ampGain, gain);
                auto out = runRig (r, sine (200.0, sr, 24000, 0.3f), 512);
                expect (allFinite (out));
                expectLessThan (peakOf (out), 4.0f);
                // energy left after removing the 200 Hz fundamental (least squares)
                double ss = 0, sc = 0, s2 = 0, c2 = 0, tot = 0;
                for (int i = 12000; i < 24000; ++i)
                {
                    const double ph = 2.0 * juce::MathConstants<double>::pi * 200.0 * i / sr;
                    ss += out[(size_t) i] * std::sin (ph); sc += out[(size_t) i] * std::cos (ph);
                    s2 += std::sin (ph) * std::sin (ph); c2 += std::cos (ph) * std::cos (ph);
                    tot += (double) out[(size_t) i] * out[(size_t) i];
                }
                const double fund = ss * ss / s2 + sc * sc / c2;
                return (tot - fund) / juce::jmax (1e-12, tot);
            };
            const double clean = thd (0.05f), dirty = thd (0.95f);
            logMessage ("  harmonic energy: gain 5% " + juce::String (clean, 4) + ", gain 95% " + juce::String (dirty, 4));
            expectGreaterThan (dirty, clean * 5.0);

            fx::AmpRig r;
            r.prepare (sr, 512);
            for (int b = 0; b < rg::numBlocks; ++b) r.setParam (rg::switchFor (b), 1.0f);   // everything on
            r.setParam (rg::driveGain, 1.0f);
            r.setParam (rg::bass, 12.0f);
            r.setParam (rg::treble, 12.0f);
            std::vector<float> right;
            auto out = runRig (r, sine (110.0, sr, 48000, 0.8f), 480, &right);
            expect (allFinite (out) && allFinite (right));
            expectLessThan (juce::jmax (peakOf (out), peakOf (right)), 8.0f);
            expectGreaterThan (peakOf (out, 24000), 0.01f);
        }

        beginTest ("delay repeats at the set time");
        {
            fx::AmpRig r;
            r.prepare (sr, 512);
            allOff (r);
            r.setParam (rg::delayOn, 1);
            r.setParam (rg::delayTime, 250.0f);
            r.setParam (rg::delayFeedback, 0.0f);
            r.setParam (rg::delayMix, 0.5f);
            r.setParam (rg::delayTone, 1.0f);
            std::vector<float> in (24000, 0.0f);
            in[100] = 1.0f;
            auto out = runRig (r, in, 256);
            int at = 0;
            float best = 0;
            for (int i = 200; i < 24000; ++i) if (std::abs (out[(size_t) i]) > best) { best = std::abs (out[(size_t) i]); at = i; }
            expect (std::abs (at - (100 + 12000)) <= 2, "echo at " + juce::String (at));
            expectGreaterThan (best, 0.2f);
        }

        beginTest ("block order is honoured (reverb before the amp runs mono afterwards)");
        {
            fx::AmpRig r;
            r.prepare (sr, 512);
            allOff (r);
            r.setParam (rg::reverbOn, 1);
            r.setParam (rg::ampOn, 1);
            auto order = rg::defaultOrder();
            std::swap (order[3], order[8]);   // reverb where the amp was, amp last
            r.order = rg::packOrder (order);
            std::vector<float> right;
            auto out = runRig (r, sine (200.0, sr, 9600, 0.3f), 512, &right);
            float maxDiff = 0;
            for (size_t i = 0; i < out.size(); ++i) maxDiff = juce::jmax (maxDiff, std::abs (out[i] - right[i]));
            expectLessThan (maxDiff, 1.0e-6f);    // the amp (mono) came last: both sides identical
            expectGreaterThan (peakOf (out, 4800), 0.01f);
        }

        beginTest ("NAM model hand-over, normalisation and garbage collection");
        {
            fx::AmpRig r;
            r.prepare (sr, 512);
            allOff (r);
            r.setParam (rg::ampOn, 1);
            r.setParam (rg::namNormalize, 0);
            juce::String err;
            auto m = fx::NamModel::load (writeLinearModel ("half.nam", { 0.5f }), err);
            m->prepare (sr, 512);
            r.setModel (std::move (m));
            expect (! r.modelActive.load());          // not picked up before the next block
            auto out = runRig (r, sine (330.0, sr, 4800), 256);
            expect (r.modelActive.load());
            expectWithinAbsoluteError (peakOf (out, 2400), 0.25f, 0.002f);

            r.setParam (rg::namNormalize, 1);        // loudness -18 dB in the file: gain 0 dB
            out = runRig (r, sine (330.0, sr, 4800), 256);
            expectWithinAbsoluteError (peakOf (out, 2400), 0.25f, 0.002f);

            auto m2 = fx::NamModel::load (writeLinearModel ("quarter.nam", { 0.25f }), err);
            m2->prepare (sr, 512);
            r.setModel (std::move (m2));
            r.setParam (rg::namNormalize, 0);
            out = runRig (r, sine (330.0, sr, 9600), 256);
            expectWithinAbsoluteError (peakOf (out, 4800), 0.125f, 0.002f);
            r.collectGarbage();                       // frees the first model on this thread

            auto wrongRate = fx::NamModel::load (writeLinearModel ("x.nam", { 1.0f }), err);
            wrongRate->prepare (44100.0, 512);
            r.setModel (std::move (wrongRate));
            out = runRig (r, sine (330.0, sr, 4800), 256);
            expect (r.modelNeedsPrepare.load());
            expect (! r.modelActive.load());          // falls back to the built-in amp, never runs a mis-prepared model
            expect (allFinite (out));

            // input calibration: interface 0 dBFS = 12 dBu, capture made at 6 dBu -> +6 dB into the model
            auto m3 = fx::NamModel::load (writeLinearModel ("cal.nam", { 1.0f }), err);
            expect (m3->info().hasInputLevel);
            expectWithinAbsoluteError (m3->info().inputLevelDbu, 6.0, 1e-9);
            m3->prepare (sr, 512);
            r.setModel (std::move (m3));
            r.setParam (rg::namNormalize, 0);
            r.setParam (rg::namCalibrate, 1);
            r.setParam (rg::calLevel, 12.0f);
            out = runRig (r, sine (330.0, sr, 9600, 0.25f), 256);
            expectWithinAbsoluteError (peakOf (out, 4800), 0.25f * 1.99526f, 0.003f);
            r.setParam (rg::namCalibrate, 0);
            out = runRig (r, sine (330.0, sr, 9600, 0.25f), 256);
            expectWithinAbsoluteError (peakOf (out, 4800), 0.25f, 0.003f);
            r.collectGarbage();

            r.clearModel();
            runRig (r, sine (330.0, sr, 512), 256);
            expect (! r.modelActive.load() && ! r.modelNeedsPrepare.load());
        }

        beginTest ("cabinet impulse response");
        {
            fx::AmpRig r;
            r.prepare (sr, 512);
            allOff (r);
            r.setParam (rg::cabOn, 1);
            r.setParam (rg::cabLowCut, 20.0f);
            r.setParam (rg::cabHighCut, 20000.0f);
            juce::AudioBuffer<float> ir (1, 64);
            ir.clear();
            ir.setSample (0, 0, 0.05f);   // a little direct sound so trimming keeps the start
            ir.setSample (0, 40, 1.0f);
            r.setImpulseResponse (std::move (ir), sr);
            r.finishLoadingOffline();
            r.reset();
            std::vector<float> in (4800, 0.0f);
            in[1000] = 1.0f;
            auto out = runRig (r, in, 256);
            int at = 0;
            float best = 0;
            for (int i = 0; i < 4800; ++i) if (std::abs (out[(size_t) i]) > best) { best = std::abs (out[(size_t) i]); at = i; }
            expectEquals (at, 1040);
            expect (r.irActive.load());
        }

        beginTest ("engine: track insert and live rig");
        {
            AudioEngine e;
            e.prepare (sr, 256);
            auto graph = std::make_unique<PlaybackGraph>();
            PlayTrack t;
            t.monitor = true;
            t.inputLeft = 0;
            t.rig = std::make_shared<fx::AmpRig>();
            t.rig->prepare (sr, 256);
            allOff (*t.rig);
            t.rig->setParam (rg::outGain, -6.0206f);
            t.rig->reset();
            graph->tracks.push_back (t);
            e.submitGraph (std::move (graph));

            std::vector<float> in (256), l (256), r (256);
            auto run = [&] (int blocks) {
                float pk = 0;
                for (int b = 0; b < blocks; ++b)
                {
                    for (int i = 0; i < 256; ++i) in[(size_t) i] = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * (b * 256 + i) / sr);
                    const float* ins[1] = { in.data() };
                    float* outs[2] = { l.data(), r.data() };
                    e.process (ins, 1, outs, 2, 256);
                    if (b >= blocks / 2) pk = juce::jmax (pk, peakOf (l));
                }
                return pk;
            };
            expectWithinAbsoluteError (run (40), 0.25f, 0.003f);     // monitored through the rig at -6 dB

            fx::AmpRig live;
            live.prepare (sr, 256);
            allOff (live);
            e.liveRig = &live;
            e.liveRigOn = true;
            e.liveInput = 0;
            expectWithinAbsoluteError (run (40), 0.75f, 0.005f);     // + live rig at 0 dB (same phase)
            e.liveRigOn = false;
            expectWithinAbsoluteError (run (40), 0.25f, 0.003f);
            e.liveRig = nullptr;
            e.collectGarbage();
        }

        beginTest ("export renders the track rig offline");
        {
            auto dir = tempFile ("exp.ffproj");
            dir.deleteRecursively();
            project::Bundle::createStructure (dir);
            auto wav = dir.getChildFile ("Audio/s.wav");
            {
                juce::AudioBuffer<float> b (1, 48000);
                for (int i = 0; i < 48000; ++i) b.setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 300.0 * i / sr));
                juce::WavAudioFormat fmt;
                std::unique_ptr<juce::OutputStream> os (wav.createOutputStream().release());
                std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (os.get(), sr, 1, 32, {}, 0));
                os.release();
                w->writeFromAudioSampleBuffer (b, 0, 48000);
            }
            auto projectTree = project::createDefault ("T");
            auto tracks = projectTree.getChildWithName (project::ids::TRACKS);
            auto t = tl::createTrack ("Gtr", 0);
            tracks.appendChild (t, nullptr);
            auto c = tl::createClip ("Audio/s.wav", "s", 0.0, 1.0, 0.0);
            t.appendChild (c, nullptr);
            auto rigTree = rg::getOrCreate (t);
            rigTree.setProperty (rg::ids::enabled, true, nullptr);
            for (int b = 0; b < rg::numBlocks; ++b) rg::set (rigTree, rg::switchFor (b), 0.0f);
            rg::set (rigTree, rg::outGain, -6.0206f);
            rigTree.setProperty (rg::ids::namFile, "/nope/missing.nam", nullptr);

            AudioCache cache;
            ExportSettings s;
            s.sampleRate = sr;
            s.bitDepth = 32;
            s.startSec = 0.0;
            s.endSec = 1.0;
            s.destination = tempFile ("rigexport.wav");
            auto res = exportAudio (tracks, dir, cache, s);
            expect (res.result.wasOk(), res.result.getErrorMessage());
            expect (res.warnings.joinIntoString (" ").contains ("missing.nam"), "missing model not reported");
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (s.destination));
            expect (rd != nullptr);
            if (rd != nullptr)
            {
                juce::AudioBuffer<float> b (2, 48000);
                rd->read (&b, 0, 48000, 0, true, true);
                expectWithinAbsoluteError (b.getMagnitude (0, 24000, 24000), 0.25f, 0.003f);
            }
            dir.deleteRecursively();
        }

        beginTest ("rig bank follows the project tree");
        {
            AudioEngine e;
            e.prepare (sr, 256);
            fx::RigBank bank (e);
            int rebuilds = 0;
            bank.onRigsChanged = [&] { ++rebuilds; };
            auto projectTree = project::createDefault ("T");
            auto tracks = projectTree.getChildWithName (project::ids::TRACKS);
            auto t = tl::createTrack ("Gtr", 0);
            tracks.appendChild (t, nullptr);
            bank.rebind (projectTree);
            const auto id = t[tl::ids::id].toString();
            expect (bank.rigForTrack (id) == nullptr);
            auto rigTree = rg::getOrCreate (t);              // created disabled
            expect (bank.rigForTrack (id) == nullptr);
            rigTree.setProperty (rg::ids::enabled, true, nullptr);
            auto rig = bank.rigForTrack (id);
            expect (rig != nullptr);
            expectEquals (rebuilds, 1);
            expect (rig->isPreparedFor (sr));
            rg::set (rigTree, rg::bass, 5.0f);
            expectEquals (rig->getParam (rg::bass), 5.0f);
            rigTree.setProperty (rg::ids::namFile, namData ("wavenet.nam").getFullPathName(), nullptr);
            expect (bank.statusFor (rigTree).modelLoading);
            e.prepare (44100.0, 128);                         // device restart: every rig follows
            expect (rig->isPreparedFor (44100.0));
            expect (bank.liveRig().isPreparedFor (44100.0));
            auto live = bank.liveTree();
            live.setProperty (rg::ids::enabled, true, nullptr);
            expect (e.liveRigOn.load());
            rigTree.setProperty (rg::ids::enabled, false, nullptr);
            expect (bank.rigForTrack (id) == nullptr);
            expectEquals (rebuilds, 2);
            tracks.removeChild (t, nullptr);
        }
    }
};

struct AmpRigPerf : juce::UnitTest
{
    AmpRigPerf() : juce::UnitTest ("Amp rig CPU", "perf") {}

    void runTest() override
    {
        beginTest ("standard WaveNet capture + full chain, real-time factor");
        juce::String err;
        auto m = fx::NamModel::load (namData ("wavenet_a1_standard.nam"), err);
        expect (m != nullptr, err);
        for (double rate : { 48000.0, 44100.0 })
        {
            fx::AmpRig r;
            r.prepare (rate, 256);
            auto mm = fx::NamModel::load (namData ("wavenet_a1_standard.nam"), err);
            mm->prepare (rate, 256);
            r.setModel (std::move (mm));
            for (int b = 0; b < rg::numBlocks; ++b) r.setParam (rg::switchFor (b), 1.0f);
            auto in = sine (110.0, rate, (int) rate * 3, 0.3f);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            auto out = runRig (r, in, 256);
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            expect (r.modelActive.load());
            expect (allFinite (out));
            logMessage ("  " + juce::String (rate) + " Hz: 3 s of audio in " + juce::String (ms, 1) + " ms ("
                        + juce::String (3000.0 / ms, 1) + "x real time)");
            // shared CI machines vary a lot: only enforce a margin when asked to (FF_PERF=1)
            if (juce::SystemStats::getEnvironmentVariable ("FF_PERF", {}).isNotEmpty())
                expectGreaterThan (3000.0 / ms, 2.0);
        }
    }
};

static RigModelTests rigModelTests;
static NamModelTests namModelTests;
static AmpRigTests ampRigTests;
static AmpRigPerf ampRigPerf;
