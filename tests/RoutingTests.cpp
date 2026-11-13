#include <juce_audio_formats/juce_audio_formats.h>
#include "core/ProjectFile.h"
#include "core/RigModel.h"
#include "core/Timeline.h"
#include "engine/AudioCache.h"
#include "engine/GraphMixer.h"
#include "engine/TrackWorkers.h"
#include "engine/fx/AmpRig.h"
#include "engine/fx/NamModel.h"
#include "engine/fx/InsertChain.h"
#include "engine/fx/BuiltinPlugins.h"
#include "engine/fx/PluginHost.h"
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


/** A test plugin with a side-chain input: silences its main signal while the side-chain carries sound. */
class DuckPlugin final : public juce::AudioPluginInstance
{
public:
    DuckPlugin() : juce::AudioPluginInstance (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo())
                                                  .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
                                                  .withOutput ("Output", juce::AudioChannelSet::stereo())) {}
    void fillInPluginDescription (juce::PluginDescription& d) const override { d.name = "Duck"; }
    const juce::String getName() const override { return "Duck"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        const bool scOn = getBusCount (true) > 1 && getBus (true, 1)->isEnabled() && b.getNumChannels() >= 4;
        const bool duck = scOn && b.getMagnitude (2, 0, b.getNumSamples()) > 0.05f;
        if (duck) { b.clear (0, 0, b.getNumSamples()); b.clear (1, 0, b.getNumSamples()); }
    }
};

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

        beginTest ("side-chain: a plugin listens to another track");
        {
            juce::ValueTree tracks ("TRACKS");
            auto kick = track (tracks, "Kick"), bass = track (tracks, "Bass");
            auto chain = std::make_shared<fx::InsertChain>();
            chain->prepare (48000.0, 256);
            auto plug = std::make_shared<DuckPlugin>();
            fx::preparePlugin (*plug, 48000.0, 256);
            chain->setPlugin (0, plug);
            std::array<int, fx::InsertChain::maxSlots> order {};
            chain->setOrder (order, 1);
            GraphBuildOptions opt;
            opt.insertsFor = [&] (const juce::ValueTree& t) { return t == bass ? chain : nullptr; };
            ConstSource src;
            src.levels = { 0.5f, 0.4f };   // kick, bass (by meter slot)
            GraphMixer m;
            m.prepare (256, 48000.0);

            auto g = buildPlaybackGraph (tracks, noBundle, cache, opt);
            expect ((int) g->tracks[1].sidechains.size() == 0);
            expectWithinAbsoluteError (mixOnce (*g, m, src), 0.9f, 1.0e-4f);   // no side-chain source: nothing ducks

            chain->sidechainId[0] = kick[tl::ids::id].toString();
            g = buildPlaybackGraph (tracks, noBundle, cache, opt);
            expectEquals ((int) g->tracks[1].sidechains.size(), 1);
            expect (g->tracks[1].level > g->tracks[0].level, "the side-chain source is processed first");
            expectWithinAbsoluteError (mixOnce (*g, m, src), 0.5f, 1.0e-4f);   // the kick silences the bass

            chain->sidechainId[0] = bass[tl::ids::id].toString();   // itself: ignored
            g = buildPlaybackGraph (tracks, noBundle, cache, opt);
            expectEquals ((int) g->tracks[1].sidechains.size(), 0);

            chain->sidechainId[0] = kick[tl::ids::id].toString();
            kick.setProperty (tl::ids::mute, true, nullptr);
            g = buildPlaybackGraph (tracks, noBundle, cache, opt);
            expectWithinAbsoluteError (mixOnce (*g, m, src), 0.4f, 1.0e-4f);   // a muted source does not trigger it
        }

        beginTest ("plugins see the host transport");
        {
            auto plug = std::make_shared<DuckPlugin>();
            fx::preparePlugin (*plug, 48000.0, 256);
            expect (plug->getPlayHead() != nullptr, "every plugin gets the transport");
            fx::TransportPlayHead::Info i;
            i.playing = true; i.samples = 96000; i.sampleRate = 48000.0; i.bpm = 90.0; i.ppq = 3.0; i.tsNum = 3; i.tsDen = 4;
            fx::transportPlayHead().update (i);
            auto pos = plug->getPlayHead()->getPosition();
            expect (pos.hasValue());
            if (pos.hasValue())
            {
                expect (pos->getIsPlaying());
                expectWithinAbsoluteError (*pos->getBpm(), 90.0, 1.0e-9);
                expectWithinAbsoluteError (*pos->getPpqPosition(), 3.0, 1.0e-9);
                expectWithinAbsoluteError (*pos->getTimeInSeconds(), 2.0, 1.0e-9);
                expectWithinAbsoluteError (*pos->getPpqPositionOfLastBarStart(), 3.0, 1.0e-9);   // 3/4 bars are 3 quarters long
                expectEquals (pos->getTimeSignature()->numerator, 3);
            }
            i.playing = false;
            fx::transportPlayHead().update (i);
            expect (! fx::transportPlayHead().getPosition()->getIsPlaying());
        }

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

            bus.setProperty (tl::ids::solo, true, nullptr);
            g = buildPlaybackGraph (tracks, noBundle, cache, {});
            expectWithinAbsoluteError (mixOnce (*g, m, src), (0.5f + 0.1f) * 0.5f, 1.0e-4f);   // solo bus: what plays into it, not C itself
            bus.setProperty (tl::ids::solo, false, nullptr);

            fx.setProperty (tl::ids::solo, true, nullptr);
            g = buildPlaybackGraph (tracks, noBundle, cache, {});
            expectWithinAbsoluteError (mixOnce (*g, m, src), 0.1f * 0.5f, 1.0e-4f);   // solo FX return: only the reverb of C
            fx.setProperty (tl::ids::solo, false, nullptr);

            c.getChildWithName (tl::ids::SEND).setProperty (tl::ids::pre, true, nullptr);
            c.setProperty (tl::ids::gainDb, -96.0, nullptr);
            g = buildPlaybackGraph (tracks, noBundle, cache, {});
            expectWithinAbsoluteError (mixOnce (*g, m, src), 0.25f + 0.05f, 1.0e-4f);   // pre-fader send ignores C's fader

            tl::removeRoutesTo (tracks, bus[tl::ids::id].toString(), nullptr);
            expect (tl::outputOf (a).isEmpty() && tl::outputOf (fx).isEmpty());
        }

        beginTest ("master plugins process the whole mix");
        {
            juce::ValueTree tracks ("TRACKS");
            track (tracks, "A");
            track (tracks, "B");
            GraphBuildOptions o;
            o.masterInserts = std::make_shared<fx::InsertChain>();
            o.masterInserts->prepare (48000.0, 256);
            std::shared_ptr<juce::AudioPluginInstance> eq (fx::builtin::create ("fretforge:eq").release());
            for (auto* prm : eq->getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm); r != nullptr && r->getParameterID() == "out")
                    r->setValueNotifyingHost (r->convertTo0to1 (6.0206f));
            fx::preparePlugin (*eq, 48000.0, 256);
            o.masterInserts->setPlugin (0, eq);
            std::array<int, fx::InsertChain::maxSlots> order {};
            o.masterInserts->setOrder (order, 1);
            auto g = buildPlaybackGraph (tracks, noBundle, cache, o);
            GraphMixer m;
            m.prepare (256, 48000.0);
            ConstSource src;
            src.levels = { 0.1f, 0.15f };
            for (int i = 0; i < 20; ++i) mixOnce (*g, m, src, 256);   // let the output gain settle
            expectWithinAbsoluteError (mixOnce (*g, m, src, 256), 0.5f, 1.0e-3f);
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

        beginTest ("interface outputs: a track or a send can play to Out 3+4 instead of the master");
        {
            juce::ValueTree tracks ("TRACKS");
            auto a = track (tracks, "Click for drummer"), b = track (tracks, "Vocal");
            a.setProperty (tl::ids::output, tl::hardwareRoute (2), nullptr);     // Out 3+4
            tl::addSend (b, tl::hardwareRoute (4), -6.0206, true, nullptr);       // headphone mix on Out 5+6, pre-fader
            b.setProperty (tl::ids::gainDb, -100.0, nullptr);                     // fader down: the pre-fader send still plays
            expect (tl::isHardwareRoute (tl::outputOf (a)) && tl::hardwareChannelOf (tl::outputOf (a)) == 2);
            expectEquals (tl::hardwareRouteName (2), juce::String ("Out 3+4"));

            auto g = buildPlaybackGraph (tracks, noBundle, cache, {});
            expectEquals (g->tracks[0].hwOutput, 2);
            expectEquals (g->tracks[0].output, -1);
            expectEquals ((int) g->tracks[1].hwSends.size(), 1);
            expectEquals ((int) g->tracks[1].sends.size(), 0);

            GraphMixer m;
            m.prepare (64, 48000.0);
            ConstSource src;
            src.levels = { 0.4f, 0.3f };
            std::vector<std::vector<float>> dev (6, std::vector<float> (64, 0.0f));
            float* hw[6];
            for (int c = 0; c < 6; ++c) hw[c] = dev[(size_t) c].data();
            std::vector<float> L (64, 0.0f), R (64, 0.0f);
            m.mix (*g, src, L.data(), R.data(), 64, hw, 6);
            expectWithinAbsoluteError (L[63], 0.0f, 1.0e-4f, "neither reaches the master");
            expectWithinAbsoluteError (dev[2][63], 0.4f, 1.0e-4f);
            expectWithinAbsoluteError (dev[3][63], 0.4f, 1.0e-4f);
            expectWithinAbsoluteError (dev[4][63], 0.15f, 1.0e-4f);
            expectWithinAbsoluteError (dev[5][63], 0.15f, 1.0e-4f);
            expectWithinAbsoluteError (dev[0][63] + dev[1][63], 0.0f, 1.0e-6f);

            // the device has no Out 3+4 (or export): the track plays through the master, the send is left out
            std::fill (L.begin(), L.end(), 0.0f);
            for (auto& d : dev) std::fill (d.begin(), d.end(), 0.0f);
            m.mix (*g, src, L.data(), R.data(), 64, hw, 2);
            expectWithinAbsoluteError (L[63], 0.4f, 1.0e-4f);
            expectWithinAbsoluteError (dev[4][63], 0.0f, 1.0e-6f);
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

            // master plugins in the export, with the tracks copied (as the app does), like a limiter on the master
            struct BuiltinHost final : fx::PluginHost
            {
                void createAsync (const juce::String& d, double r, int b, std::function<void (std::unique_ptr<juce::AudioPluginInstance>, juce::String)> done) override
                { juce::String e; done (createBlocking (d, r, b, e), e); }
                std::unique_ptr<juce::AudioPluginInstance> createBlocking (const juce::String& d, double r, int b, juce::String&) override
                {
                    juce::PluginDescription pd;
                    if (auto x = juce::parseXML (d)) pd.loadFromXml (*x);
                    auto p = fx::builtin::create (pd.fileOrIdentifier);
                    if (p != nullptr) p->setRateAndBufferSizeDetails (r, b);
                    return p;
                }
            } host;
            fx::setPluginHost (&host);
            t.setProperty (tl::ids::gainDb, 6.0, nullptr);
            bus.setProperty (tl::ids::gainDb, 6.0, nullptr);   // 0.5 * 2 * 2 = +6 dB over full scale
            auto master = project::masterNode (projectTree);
            rg::addInsert (master, fx::builtin::descriptionXml ("fretforge:limiter"), "Limiter", nullptr,
                           fx::builtin::stateWith ("fretforge:limiter", { { "ceiling", -1.0f } }));
            ExportSettings s;
            s.sampleRate = sr;
            s.bitDepth = 32;
            s.endSec = 1.0;
            s.destination = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ff_routing_limited.wav");
            s.master = master.createCopy();
            auto res = exportAudio (tracks.createCopy(), dir, cache, s);
            fx::setPluginHost (nullptr);
            expect (res.result.wasOk(), res.result.getErrorMessage());
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (s.destination));
            expect (rd != nullptr);
            if (rd != nullptr)
            {
                juce::AudioBuffer<float> b (2, 48000);
                rd->read (&b, 0, 48000, 0, true, true);
                const float pk = b.getMagnitude (0, 48000);
                expect (pk <= juce::Decibels::decibelsToGain (-1.0f) + 1.0e-5f && pk > 0.8f, "limited export peak " + juce::String (pk));
            }
            dir.deleteRecursively();
        }
    }
};

static RoutingTests routingTests;
