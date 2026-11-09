#include <juce_core/juce_core.h>
#include "core/Exercises.h"
#include "core/TabModel.h"
#include "engine/TabSynth.h"

namespace
{
class ExerciseTests : public juce::UnitTest
{
public:
    ExerciseTests() : juce::UnitTest ("Training / exercise generator", "Training") {}

    void runTest() override
    {
        namespace tr = ff::training;
        namespace tab = ff::tab;

        beginTest ("every technique makes a playable pattern, every seed and position");
        {
            expectEquals ((int) tr::techniques().size(), 27);
            for (int t = 0; t < (int) tr::techniques().size(); ++t)
                for (juce::uint32 seed : { 1u, 2u, 7u, 99u })
                    for (int pos : { 0, 3, 5, 12 })
                    {
                        tr::Settings s;
                        s.technique = t;
                        s.seed = seed;
                        s.position = pos;
                        const auto p = tr::pattern (s);
                        expect (! p.empty(), juce::String (tr::techniques()[(size_t) t].id));
                        int notes = 0;
                        for (auto& st : p)
                            for (auto& n : st.notes)
                            {
                                ++notes;
                                expect (n.string >= 0 && n.string < 6 && n.fret >= 0 && n.fret <= 24,
                                        juce::String (tr::techniques()[(size_t) t].id) + " fret " + juce::String (n.fret));
                            }
                        expect (notes > 0);
                    }
            for (auto& p : tr::plans())
                for (auto* id : p.techniques) expect (tr::techniqueIndex (id) >= 0, id);
        }

        beginTest ("minor pentatonic: the five shapes, as on the chart");
        {
            tr::Settings s;
            s.position = 5;   // A minor
            const char* ids[] { "pentatonic", "pent2", "pent3", "pent4", "pent5" };
            // low E string frets of each shape in A (root at fret 5): 5-8, 8-10, 10-12, 12-15, 15-17
            const int lowE[5][2] { { 5, 8 }, { 8, 10 }, { 10, 12 }, { 12, 15 }, { 15, 17 } };
            for (int k = 0; k < 5; ++k)
            {
                s.technique = tr::techniqueIndex (ids[k]);
                const auto d = tr::scaleDiagram (s);
                expectEquals ((int) d.size(), 12, ids[k]);
                expectEquals (d[0].string, 0);
                expectEquals (d[0].fret, lowE[k][0], ids[k]);
                expectEquals (d[1].fret, lowE[k][1], ids[k]);
                int roots = 0;
                for (auto& n : d)
                {
                    const int pitch = s.tuning[(size_t) n.string] + n.fret;
                    const int pc = ((pitch - 45) % 12 + 12) % 12;   // A = 0
                    const juce::String deg (n.degree);
                    const int want = deg == "1" ? 0 : deg == "b3" ? 3 : deg == "4" ? 5 : deg == "5" ? 7 : 10;
                    expectEquals (pc, want, juce::String (ids[k]) + " degree " + deg);
                    roots += deg == "1" ? 1 : 0;
                }
                expect (roots >= 2, "every shape has at least two roots");
                expectEquals ((int) tr::pattern (s).size(), 22, "up and down: 12 notes up, 10 back");
            }
            s.position = 15;   // high key: shape 5 moves down an octave
            s.technique = tr::techniqueIndex ("pent5");
            for (auto& n : tr::scaleDiagram (s)) expect (n.fret <= 22);
            s.technique = tr::techniqueIndex ("spider");
            expect (tr::scaleDiagram (s).empty());
        }

        beginTest ("seeds vary the pattern, the same seed repeats it");
        {
            tr::Settings a;
            a.technique = tr::techniqueIndex ("spider");
            a.seed = 5;
            auto b = a;
            expect (tr::asciiTab (a) == tr::asciiTab (b));
            int different = 0;
            for (juce::uint32 seed = 2; seed < 12; ++seed)
            {
                b.seed = seed;
                if (tr::asciiTab (b) != tr::asciiTab (a)) ++different;
            }
            expect (different >= 5);
        }

        beginTest ("the spider is 1-2-3-4 on every string at the position");
        {
            tr::Settings s;
            s.technique = tr::techniqueIndex ("spider");
            s.position = 5;
            const auto p = tr::pattern (s);
            expectEquals ((int) p.size(), 48);
            for (int i = 0; i < 4; ++i)
            {
                expectEquals (p[(size_t) i].notes[0].string, 0);
                expectEquals (p[(size_t) i].notes[0].fret, 5 + i);
            }
        }

        beginTest ("legato marks hammer-ons, palm-muted rhythms are muted, a score plays");
        {
            tr::Settings s;
            s.technique = tr::techniqueIndex ("legato");
            const auto p = tr::pattern (s);
            expect (! p[0].notes[0].hammer && p[1].notes[0].hammer && p[2].notes[0].hammer && ! p[3].notes[0].hammer);
            s.technique = tr::techniqueIndex ("chugs");
            bool muted = false;
            for (auto& st : tr::pattern (s)) for (auto& n : st.notes) muted |= n.palmMute;
            expect (muted);

            for (int sub : { 8, 12, 16 })
            {
                s.technique = tr::techniqueIndex ("alternate");
                s.subdivision = sub;
                s.bars = 2;
                auto score = tr::buildScore (s, 100.0);
                expectEquals (tab::numMeasures (score), 2);
                const auto track = tab::tracks (score).getChild (0);
                expectWithinAbsoluteError (tab::barContentQ (tab::bar (track, 0)), 4.0, 1.0e-6);   // every bar exactly full
                auto seq = ff::buildTabSequence (score, 1.0);
                expect (seq != nullptr && ! seq->notes.empty());
                expectWithinAbsoluteError (seq->lengthSec, 2 * 4 * 0.6, 0.01);   // 2 bars of 4/4 at 100 BPM
                expectEquals ((int) seq->notes.size(), sub == 8 ? 16 : sub == 12 ? 24 : 32);
            }
        }
    }
};

static ExerciseTests exerciseTests;
} // namespace
