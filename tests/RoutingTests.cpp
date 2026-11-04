#include <juce_audio_formats/juce_audio_formats.h>
#include "core/ProjectFile.h"
#include "core/RigModel.h"
#include "core/Timeline.h"
#include "engine/AudioCache.h"
#include "engine/GraphMixer.h"
#include "engine/TrackWorkers.h"
#include "engine/fx/AmpRig.h"
#include "engine/fx/NamModel.h"
#include "engine/Exporter.h"
#include <cmath>

using namespace ff;
namespace tl = ff::timeline;
namespace rg = ff::rig;

namespace
{
/** Audio tracks play a constant level (their meterSlot picks it from `levels`). */
struct ConstSource final : GraphMixer::Source
{
    std::vector<float> levels;
    bool fill (int, PlayTrack& t, float* L, float* R, int n) noexcept override
    {
        if (! t.audible) return false;
        if (t.kind != tl::TrackKind::audio) return true;
        for (int i = 0; i < n; ++i) { L[i] += levels[(size_t) t.meterSlot]; R[i] += levels[(size_t) t.meterSlot]; }
        return true;
    }
};

juce::ValueTree track (juce::ValueTree tracks, const juce::String& name, tl::TrackKind k = tl::TrackKind::audio)
{
    auto t = k == tl::TrackKind::audio ? tl::createTrack (name, 0) : tl::createBusTrack (name, 0, k);
    tracks.appendChild (t, nullptr);
    return t;
}

float mixOnce (PlaybackGraph& g, GraphMixer& m, ConstSource& src, int n = 64)
{
    std::vector<float> L ((size_t) n, 0.0f), R ((size_t) n, 0.0f);
    m.mix (g, src, L.data(), R.data(), n);
    return L[(size_t) n - 1];
}

juce::File namData (const juce::String& name) { return juce::File (FF_TEST_DATA_DIR).getChildFile ("nam").getChildFile (name); }
}

class RoutingTests final : public juce::UnitTest
{
public:
    RoutingTests() : juce::UnitTest ("Bus and FX routing", "Engine") {}

    void runTest() override
    {
        AudioCache cache;
        const juce::File noBundle;

        beginTest ("graph: outputs, sends and processing order");
        {
            juce::ValueTree tracks ("TRACKS");
            auto a = track (tracks, "A"), c = track (tracks, "C");
            auto bus = track (tracks, "Bus", tl::TrackKind::bus), fx = track (tracks, "Verb", tl::TrackKind::fx);
            a.setProperty (tl::ids::output, bus[tl::ids::id], nullptr);
            bus.setProperty (tl::ids::gainDb, -6.0206, nullptr);
            tl::addSend (c, fx[tl::ids::id].toString(), -6.0206, false, nullptr);
            expect (tl::canRouteTo (tracks, a, bus));
            expect (! tl::canRouteTo (tracks, bus, a), "audio tracks take no input");
            expect (tl::canRouteTo (tracks, bus, fx));
            fx.setProperty (tl::ids::output, bus[tl::ids::id], nullptr);
            expect (! tl::canRouteTo (tracks, bus, fx), "bus -> fx -> bus is a loop");

            auto g = buildPlaybackGraph (tracks, noBundle, cache, {});
            expectEquals ((int) g->tracks.size(), 4);
            expectEquals (g->tracks[0].output, 2);
            expectEquals ((int) g->tracks[1].sends.size(), 1);
            expectEquals (g->tracks[1].sends[0].target, 3);
            expectEquals (g->tracks[3].output, 2);
            expect (g->tracks[2].level > g->tracks[3].level && g->tracks[3].level > g->tracks[0].level, "buses after their sources");

            GraphMixer m;
            m.prepare (256, 48000.0);
            ConstSource src;
            src.levels = { 0.5f, 0.2f, 0.0f, 0.0f };
            // A 0.5 and Verb 0.1 (C sent post-fader at -6 dB) through the bus at -6 dB, plus C 0.2 straight
            expectWithinAbsoluteError (mixOnce (*g, m, src), 0.5f * 0.5f + 0.1f * 0.5f + 0.2f, 1.0e-4f);

            bus.setProperty (tl::ids::mute, true, nullptr);
            g = buildPlaybackGraph (tracks, noBundle, cache, {});
            expectWithinAbsoluteError (mixOnce (*g, m, src), 0.2f, 1.0e-4f);   // the muted bus silences what plays into it
            bus.setProperty (tl::ids::mute, false, nullptr);

            c.setProperty (tl::ids::solo, true, nullptr);
            g = buildPlaybackGraph (tracks, noBundle, cache, {});
            expectWithinAbsoluteError (mixOnce (*g, m, src), 0.2f + 0.1f * 0.5f, 1.0e-4f);   // solo C: its send still reaches the FX return
            c.setProperty (tl::ids::solo, false, nullptr);

            c.getChildWithName (tl::ids::SEND).setProperty (tl::ids::pre, true, nullptr);
            c.setProperty (tl::ids::gainDb, -96.0, nullptr);
            g = buildPlaybackGraph (tracks, noBundle, cache, {});
            expectWithinAbsoluteError (mixOnce (*g, m, src), 0.25f + 0.05f, 1.0e-4f);   // pre-fader send ignores C's fader

            tl::removeRoutesTo (tracks, bus[tl::ids::id].toString(), nullptr);
            expect (tl::outputOf (a).isEmpty() && tl::outputOf (fx).isEmpty());
        }

        beginTest ("graph: a routing loop is cut, not followed");
        {
            juce::ValueTree tracks ("TRACKS");
            auto a = track (tracks, "A");
            auto b1 = track (tracks, "B1", tl::TrackKind::bus), b2 = track (tracks, "B2", tl::TrackKind::bus);
            a.setProperty (tl::ids::output, b1[tl::ids::id], nullptr);
            b1.setProperty (tl::ids::output, b2[tl::ids::id], nullptr);
            b2.setProperty (tl::ids::output, b1[tl::ids::id], nullptr);   // written by hand: the UI never allows it
            auto g = buildPlaybackGraph (tracks, noBundle, cache, {});
            expect (g->tracks[1].output == -1 || g->tracks[2].output == -1, "one route of the loop is dropped");
            GraphMixer m;
            m.prepare (256, 48000.0);
            ConstSource src;
            src.levels = { 0.5f, 0.0f, 0.0f };
            expectWithinAbsoluteError (mixOnce (*g, m, src), 0.5f, 1.0e-4f);
        }

        beginTest ("parallel tracks: same result as on one thread");
        {
            juce::ValueTree tracks ("TRACKS");
            for (int i = 0; i < 6; ++i) track (tracks, "T" + juce::String (i));
            auto build = [&] {
                GraphBuildOptions o;
                o.rigFor = [] (const juce::ValueTree& t) {
                    auto r = std::make_shared<fx::AmpRig>();
                    r->prepare (48000.0, 256);
                    for (int b = 0; b < rg::numBlocks; ++b) r->setParam (rg::switchFor (b), 0.0f);
                    r->setParam (rg::switchFor (rg::drive), 1.0f);   // something that does real work
                    r->setParam (rg::outGain, -(float) t.getParent().indexOf (t));
                    r->reset();
                    return r;
                };
                return buildPlaybackGraph (tracks, noBundle, cache, o);
            };
            auto g1 = build(), g2 = build();
            ConstSource src;
            src.levels = { 0.3f, 0.2f, 0.25f, 0.1f, 0.15f, 0.05f };
            GraphMixer serial, parallel;
            serial.prepare (256, 48000.0);
            parallel.prepare (256, 48000.0);
            TrackWorkers workers;
            workers.start (3, 48000.0, 256);
            parallel.workers = &workers;
            expectEquals (workers.size(), 3);
            float maxDiff = 0.0f;
            std::vector<float> l1 (256), r1 (256), l2 (256), r2 (256);
            for (int b = 0; b < 200; ++b)
            {
                std::fill (l1.begin(), l1.end(), 0.0f); std::fill (r1.begin(), r1.end(), 0.0f);
                std::fill (l2.begin(), l2.end(), 0.0f); std::fill (r2.begin(), r2.end(), 0.0f);
                src.levels[0] = 0.3f * (float) std::sin (b * 0.37);
                serial.mix (*g1, src, l1.data(), r1.data(), 256);
                parallel.mix (*g2, src, l2.data(), r2.data(), 256);
                for (int i = 0; i < 256; ++i) maxDiff = juce::jmax (maxDiff, std::abs (l1[(size_t) i] - l2[(size_t) i]));
            }
            expectEquals (maxDiff, 0.0f);
            workers.stop();
        }

        beginTest ("idle rigs stop running once silent and wake up on sound");
        {
            juce::String err;
            auto model = fx::NamModel::load (namData ("wavenet.nam"), err);
            expect (model != nullptr, err);
            juce::ValueTree tracks ("TRACKS");
            track (tracks, "Gtr");
            std::shared_ptr<fx::AmpRig> rig;
            GraphBuildOptions o;
            o.rigFor = [&] (const juce::ValueTree&) {
                rig = std::make_shared<fx::AmpRig>();
                rig->prepare (48000.0, 256);
                rig->setModel (std::move (model));
                return rig;
            };
            auto g = buildPlaybackGraph (tracks, noBundle, cache, o);
            GraphMixer m;
            m.prepare (256, 48000.0);
            ConstSource src;
            src.levels = { 0.0f };
            std::vector<float> L (256), R (256);
            float lastPeak = 1.0f;
            for (int b = 0; b < 48000 * 6 / 256; ++b)
            {
                std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f);
                m.mix (*g, src, L.data(), R.data(), 256);
                lastPeak = juce::FloatVectorOperations::findMaximum (L.data(), 256);
            }
            expect (g->tracks[0].idle.sleeping, "the rig went idle after 6 s of silence (output peak " + juce::String (lastPeak) + ")");
            src.levels = { 0.1f };
            std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f);
            m.mix (*g, src, L.data(), R.data(), 256);
            expect (! g->tracks[0].idle.sleeping, "sound wakes it up");
        }

        beginTest ("export plays tracks through their bus");
        {
            const double sr = 48000.0;
            auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ff_routing_test.ffproj");
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
            auto t = track (tracks, "Gtr");
            t.appendChild (tl::createClip ("Audio/s.wav", "s", 0.0, 1.0, 0.0), nullptr);
            auto bus = track (tracks, "Bus", tl::TrackKind::bus);
            bus.setProperty (tl::ids::gainDb, -6.0206, nullptr);
            t.setProperty (tl::ids::output, bus[tl::ids::id], nullptr);

            for (bool stems : { false, true })
            {
                ExportSettings s;
                s.sampleRate = sr;
                s.bitDepth = 32;
                s.startSec = 0.0;
                s.endSec = 1.0;
                s.stems = stems;
                s.baseName = "x";
                s.destination = stems ? juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ff_routing_stems")
                                      : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ff_routing_mix.wav");
                s.destination.deleteRecursively();
                auto res = exportAudio (tracks, dir, cache, s);
                expect (res.result.wasOk(), res.result.getErrorMessage());
                expectEquals (res.files.size(), 1, "one stem: the bus has none of its own");
                juce::AudioFormatManager fm;
                fm.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (res.files[0]));
                expect (rd != nullptr);
                if (rd != nullptr)
                {
                    juce::AudioBuffer<float> b (2, 48000);
                    rd->read (&b, 0, 48000, 0, true, true);
                    expectWithinAbsoluteError (b.getMagnitude (0, 24000, 24000), 0.25f, 0.003f);
                }
            }
            dir.deleteRecursively();
        }
    }
};

static RoutingTests routingTests;
