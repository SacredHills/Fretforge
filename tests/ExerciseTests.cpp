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
            expectEquals ((int) tr::techniques().size(), 23);
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
