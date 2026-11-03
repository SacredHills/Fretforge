#include <juce_audio_basics/juce_audio_basics.h>
#include "core/DrumModel.h"
#include "engine/DrumSynth.h"
#include "engine/TabSynth.h"
#include "engine/AudioEngine.h"
#include <cmath>
#include <set>

using namespace ff;
namespace d = ff::drums;

namespace
{
std::set<std::pair<int, int>> hitSet (const juce::ValueTree& p)
{
    std::set<std::pair<int, int>> s;
    for (auto h : p) s.insert ({ (int) h[d::ids::lane], (int) h[d::ids::tick] });
    return s;
}
int countLane (const juce::ValueTree& p, int lane)
{
    int n = 0;
    for (auto h : p) n += (int) h[d::ids::lane] == lane ? 1 : 0;
    return n;
}
float peak (const juce::AudioBuffer<float>& b, int from, int to)
{
    float m = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = juce::jmax (0, from); i < juce::jmin (b.getNumSamples(), to); ++i) m = juce::jmax (m, std::abs (b.getSample (ch, i)));
    return m;
}
double energy (const juce::AudioBuffer<float>& b, int from, int to)
{
    double e = 0.0;
    for (int i = juce::jmax (0, from); i < juce::jmin (b.getNumSamples(), to); ++i) e += b.getSample (0, i) * b.getSample (0, i);
    return e;
}
} // namespace

struct DrumTests : juce::UnitTest
{
    DrumTests() : juce::UnitTest ("Drum studio", "core") {}

    void runTest() override
    {
        beginTest ("model: patterns, hits, bars, GM mapping");
        juce::ValueTree project ("PROJECT");
        auto drums = d::getOrCreate (project);
        auto p = d::addPattern (drums, "Beat", 4, 4, 1, nullptr);
        expectEquals (d::barTicks (p), 3840);
        expectEquals (d::stepTicks (p), 240);
        d::setHit (p, d::kick, 0, 110, nullptr);
        d::setHit (p, d::snare, 960, 100, nullptr);
        d::setHit (p, d::snare, 960, 64, nullptr);          // same spot: velocity changes, no duplicate
        expectEquals (p.getNumChildren(), 2);
        expectEquals ((int) d::hitAt (p, d::snare, 960)[d::ids::vel], 64);
        d::setHit (p, d::hhClosed, 3840 + 480, 90, nullptr);
        d::setBars (p, 2, nullptr);
        d::setHit (p, d::hhClosed, 3840 + 480, 90, nullptr);
        expectEquals (p.getNumChildren(), 3);
        d::setBars (p, 1, nullptr);
        expectEquals (p.getNumChildren(), 2, "hits past the end are dropped");
        for (int i = 0; i < d::numPieces; ++i)
            expectEquals (d::pieceForGmNote (d::pieceInfo (i).gmNote), i);
        expectEquals (d::pieceForGmNote (40), (int) d::snare);
        expectEquals (d::pieceForGmNote (20), -1);

        beginTest ("compile: repeats, swing, song sections, humanize");
        {
            auto c = d::compile (drums, p, 3);
            expectEquals ((int) c.events.size(), 6);
            expectEquals (c.lengthQ, 12.0);
            expectEquals ((int) c.bars.size(), 3);
            expectEquals (c.events[2].q, 4.0);
            expectEquals (d::swungTick (480, 0.5, 2), 480 + 240);  // 8th swing
            expectEquals (d::swungTick (240, 0.5, 4), 240 + 120);  // 16th swing
            expectEquals (d::swungTick (960, 0.5, 4), 960);        // on the beat: unchanged
            auto p2 = d::addPattern (drums, "Fill", 3, 4, 1, nullptr);
            d::setHit (p2, d::tomFloor, 0, 100, nullptr);
            d::addSection (drums, p[d::ids::id].toString(), 2, -1, nullptr);
            d::addSection (drums, p2[d::ids::id].toString(), 1, -1, nullptr);
            d::addSection (drums, "missing", 4, -1, nullptr);   // ignored
            auto songC = d::compile (drums, {}, 1);
            expectEquals (songC.lengthQ, 4.0 * 2 + 3.0);
            expectEquals ((int) songC.events.size(), 5);
            expectEquals (songC.events.back().piece, (int) d::tomFloor);
            expectEquals (songC.events.back().q, 8.0);
            expectEquals (songC.bars[2].num, 3);

            drums.setProperty (d::ids::humanizeMs, 10.0, nullptr);
            drums.setProperty (d::ids::humanizeVel, 0.2, nullptr);
            auto h1 = d::compile (drums, p, 4), h2 = d::compile (drums, p, 4);
            bool varied = false;
            for (size_t i = 0; i < h1.events.size(); ++i)
            {
                expectEquals (h1.events[i].offsetSec, h2.events[i].offsetSec);  // deterministic
                expect (std::abs (h1.events[i].offsetSec) <= 0.010 + 1e-9);
                varied = varied || std::abs (h1.events[i].offsetSec) > 1e-4;
            }
            expect (varied, "humanize moves hits");
            drums.setProperty (d::ids::humanizeMs, 0.0, nullptr);
            drums.setProperty (d::ids::humanizeVel, 0.0, nullptr);
        }

        beginTest ("generator: every genre, deterministic, genre rules");
        for (int g = 0; g < d::numGenres; ++g)
        {
            d::GenOptions o;
            o.genre = g;
            o.bars = 2;
            o.seed = 7;
            auto a = d::generate (o), b = d::generate (o);
            expect (hitSet (a) == hitSet (b), juce::String ("deterministic: ") + d::genreName (g));
            expect (a.getNumChildren() >= 8, juce::String ("has hits: ") + d::genreName (g));
            expect (countLane (a, d::kick) > 0, juce::String ("kick: ") + d::genreName (g));
            for (auto h : a)
            {
                expect ((int) h[d::ids::tick] < d::lengthTicks (a));
                expect ((int) h[d::ids::vel] >= 1 && (int) h[d::ids::vel] <= 127);
            }
            o.seed = 8;
            o.complexity = 0.9;
            juce::ignoreUnused (d::generate (o));
        }
        {
            d::GenOptions o;
            o.genre = d::genreRock;
            o.fill = false;
            o.crashOnOne = false;
            o.complexity = 0.0;
            o.bars = 1;
            auto rock = d::generate (o);
            expect (d::hitAt (rock, d::snare, 960).isValid() && d::hitAt (rock, d::snare, 2880).isValid(), "backbeat on 2 and 4");
            expect (d::hitAt (rock, d::kick, 0).isValid() && d::hitAt (rock, d::kick, 1920).isValid(), "kick on 1 and 3");
            expectEquals (countLane (rock, d::hhClosed), 8, "8th hats");
            o.halfTime = true;
            auto half = d::generate (o);
            expect (d::hitAt (half, d::snare, 1920).isValid() && ! d::hitAt (half, d::snare, 960).isValid(), "half-time snare on 3");

            o = {};
            o.genre = d::genreBlast; o.complexity = 0.9; o.fill = false; o.crashOnOne = false; o.bars = 1;
            auto blast = d::generate (o);
            expectEquals (countLane (blast, d::kick), 16, "bomb blast: 16th kicks");
            expectEquals (countLane (blast, d::snare), 8, "bomb blast: 8th snares");

            o.genre = d::genreDBeat; o.complexity = 0.0;
            auto db = d::generate (o);
            for (int t : { 0, 1440, 1920 }) expect (d::hitAt (db, d::kick, t).isValid(), "d-beat kick " + juce::String (t));
            for (int t : { 960, 2880 }) expect (d::hitAt (db, d::snare, t).isValid(), "d-beat snare " + juce::String (t));

            o.genre = d::genreDjent; o.complexity = 0.0; o.bars = 3;
            auto dj = d::generate (o);
            for (auto h : dj)
                if ((int) h[d::ids::lane] == d::kick) expectEquals (((int) h[d::ids::tick] / 240) % 3, 0, "3-over-4 kick grouping");

            o = {};
            o.genre = d::genreMetal; o.bars = 2; o.fill = true; o.crashOnOne = true;
            auto withFill = d::generate (o);
            expect (d::hitAt (withFill, d::crash, 0).isValid(), "crash on 1");
            int toms = 0;
            for (auto h : withFill)
                if ((int) h[d::ids::tick] >= 3840 + 2880 && (int) h[d::ids::lane] >= d::tomHigh && (int) h[d::ids::lane] <= d::tomFloor) ++toms;
            expect (toms >= 2, "fill uses toms in the last beat");

            o.genre = d::genreJazz;
            expectEquals ((int) d::generate (o)[d::ids::grid], 3, "triplet grid for swing");
            o.tsNum = 6; o.tsDen = 8; o.genre = d::genreRock; o.bars = 1;
            auto six = d::generate (o);
            expectEquals (d::barTicks (six), 2880);
        }

        beginTest ("MIDI export / import round trip");
        {
            d::GenOptions o;
            o.genre = d::genreFunk;
            o.bars = 2;
            auto g = d::generate (o);
            juce::ValueTree proj2 ("PROJECT");
            auto dr = d::getOrCreate (proj2);
            d::patterns (dr).appendChild (g, nullptr);
            auto f = juce::File::createTempFile ("mid");
            expect (d::exportMidi (dr, g, 100.0, f).wasOk());
            juce::String id;
            auto r = d::importMidi (dr, f, nullptr, id);
            expect (r.wasOk(), r.getErrorMessage());
            auto back = d::patternById (dr, id);
            expect (back.isValid());
            expectEquals ((int) back[d::ids::bars], 2);
            std::set<std::tuple<int, int, int>> a, b;
            for (auto h : g) a.insert ({ (int) h[d::ids::lane], (int) h[d::ids::tick], (int) h[d::ids::vel] });
            for (auto h : back) b.insert ({ (int) h[d::ids::lane], (int) h[d::ids::tick], (int) h[d::ids::vel] });
            expect (a == b, "hits survive the round trip");
            f.deleteFile();
        }

        beginTest ("synth: every piece sounds, stays bounded, hats choke");
        {
            constexpr double sr = 48000.0;
            for (int piece = 0; piece < d::numPieces; ++piece)
            {
                juce::AudioBuffer<float> buf;
                DrumSynth::renderOffline ({ { 0.01, piece, 1.0f } }, sr, 1.0, 1.0f, buf);
                const float pk = peak (buf, 0, buf.getNumSamples());
                expect (pk > 0.05f && pk < 1.6f, juce::String (d::pieceInfo (piece).name) + " peak " + juce::String (pk));
                expect (peak (buf, 0, (int) (0.009 * sr)) == 0.0f, "silent before the hit");
                bool finite = true;
                for (int i = 0; i < buf.getNumSamples(); ++i) finite = finite && std::isfinite (buf.getSample (0, i));
                expect (finite);
            }
            // a kick is low, a closed hat is bright: count zero crossings in the first 30 ms
            auto zc = [&] (int piece) {
                juce::AudioBuffer<float> b;
                DrumSynth::renderOffline ({ { 0.0, piece, 1.0f } }, sr, 0.05, 1.0f, b);
                int n = 0;
                for (int i = 1; i < (int) (0.03 * sr); ++i) n += (b.getSample (0, i - 1) < 0) != (b.getSample (0, i) < 0) ? 1 : 0;
                return n;
            };
            expect (zc (d::kick) < 20, "kick is low: " + juce::String (zc (d::kick)));
            expect (zc (d::hhClosed) > 200, "hat is bright: " + juce::String (zc (d::hhClosed)));
            juce::AudioBuffer<float> open, choked;
            DrumSynth::renderOffline ({ { 0.0, d::hhOpen, 1.0f } }, sr, 1.0, 1.0f, open);
            DrumSynth::renderOffline ({ { 0.0, d::hhOpen, 1.0f }, { 0.2, d::hhPedal, 0.01f } }, sr, 1.0, 1.0f, choked);
            expect (energy (choked, (int) (0.3 * sr), (int) (0.6 * sr)) < 0.01 * energy (open, (int) (0.3 * sr), (int) (0.6 * sr)),
                    "pedal hat chokes the open hat");
            // softer hits are quieter
            juce::AudioBuffer<float> soft, loud;
            DrumSynth::renderOffline ({ { 0.0, d::snare, 0.3f } }, sr, 0.3, 1.0f, soft);
            DrumSynth::renderOffline ({ { 0.0, d::snare, 1.0f } }, sr, 0.3, 1.0f, loud);
            expect (peak (soft, 0, soft.getNumSamples()) < 0.3f * peak (loud, 0, loud.getNumSamples()));
        }

        beginTest ("engine plays drum sequences on time");
        {
            juce::ValueTree proj3 ("PROJECT");
            auto dr = d::getOrCreate (proj3);
            auto pat = d::addPattern (dr, "Q", 4, 4, 1, nullptr);
            for (int b = 0; b < 4; ++b) d::setHit (pat, d::sideStick, b * 960, 127, nullptr);
            auto seq = buildDrumSequence (dr, pat, 120.0);
            expectEquals ((int) seq->drumHits.size(), 4);
            expectWithinAbsoluteError (seq->drumHits[3].sec, 1.5, 1e-9);
            expectEquals ((int) seq->clicks.size(), 4);
            expectWithinAbsoluteError (seq->lengthSec, 2.0, 1e-9);

            constexpr double sr = 48000.0;
            AudioEngine e;
            e.prepare (sr, 512);
            e.tabMode = true;
            e.submitTabSequence (std::move (seq));
            std::vector<float> l (512), r (512), all;
            float* outs[2] = { l.data(), r.data() };
            e.play();
            for (int i = 0; i < (int) (2.0 * sr / 512); ++i)
            {
                e.process (nullptr, 0, outs, 2, 512);
                all.insert (all.end(), l.begin(), l.end());
            }
            std::vector<int> onsets;
            for (int i = 0; i < (int) all.size(); ++i)
            {
                if (std::abs (all[(size_t) i]) <= 1.0e-3f) continue;
                bool quietBefore = true;   // an onset follows at least 10 ms below the threshold
                for (int k = juce::jmax (0, i - 480); k < i && quietBefore; ++k) quietBefore = std::abs (all[(size_t) k]) <= 1.0e-3f;
                if (quietBefore && (onsets.empty() || i - onsets.back() > 4800)) onsets.push_back (i);
            }
            expectEquals ((int) onsets.size(), 4);
            for (int i = 0; i < juce::jmin (4, (int) onsets.size()); ++i)
                expect (std::abs (onsets[(size_t) i] - i * 24000) <= 3, "hit " + juce::String (i) + " at " + juce::String (onsets[(size_t) i]));
            e.collectGarbage();
        }
    }
};

static DrumTests drumTests;
