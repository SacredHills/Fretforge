#include <juce_core/juce_core.h>
#include "core/Timeline.h"
#include "core/ProjectFile.h"

using namespace ff::timeline;

struct TimelineTests : juce::UnitTest
{
    TimelineTests() : juce::UnitTest ("Timeline layering", "core") {}

    static ClipSpan span (double start, double len, double offset = 0, double fi = 0, double fo = 0, int index = 0)
    {
        ClipSpan s; s.start = start; s.length = len; s.offset = offset; s.fadeIn = fi; s.fadeOut = fo; s.index = index; return s;
    }

    void runTest() override
    {
        beginTest ("single clip passes through with its own fades");
        {
            auto p = visiblePieces ({ span (1, 4, 2, 0.1, 0.2) });
            expectEquals ((int) p.size(), 1);
            expectEquals (p[0].start, 1.0); expectEquals (p[0].end, 5.0); expectEquals (p[0].srcAtStart, 2.0);
            expectWithinAbsoluteError (p[0].fadeInEnd, 1.1, 1e-9);
            expectWithinAbsoluteError (p[0].fadeOutStart, 4.8, 1e-9);
        }

        beginTest ("upper clip hides lower clip, lower is split around it");
        {
            auto p = visiblePieces ({ span (0, 10, 0, 0, 0, 0), span (4, 2, 0, 0, 0, 1) });
            expectEquals ((int) p.size(), 3);
            expectEquals (p[0].clip, 0); expectEquals (p[0].end, 4.0);
            expectEquals (p[1].clip, 1); expectEquals (p[1].start, 4.0); expectEquals (p[1].end, 6.0);
            expectEquals (p[2].clip, 0); expectEquals (p[2].start, 6.0); expectEquals (p[2].srcAtStart, 6.0);
        }

        beginTest ("fades on the upper clip become crossfades on the lower one");
        {
            auto p = visiblePieces ({ span (0, 10, 0, 0, 0, 0), span (4, 2, 0, 0.5, 0.25, 1) });
            expectEquals ((int) p.size(), 3);
            // lower clip continues under the upper clip's fade-in and fades out over it
            expectWithinAbsoluteError (p[0].end, 4.5, 1e-9);
            expectWithinAbsoluteError (p[0].fadeOutStart, 4.0, 1e-9);
            expectWithinAbsoluteError (p[0].fadeOutEnd, 4.5, 1e-9);
            // lower clip resumes during the upper clip's fade-out, fading in
            expectWithinAbsoluteError (p[2].start, 5.75, 1e-9);
            expectWithinAbsoluteError (p[2].fadeInEnd, 6.0, 1e-9);
            // equal power: gains squared sum to one in the middle of the crossfade
            const double t = 4.25;
            const double gLow = fadeGain (t, p[0].fadeOutStart, p[0].fadeOutEnd, false);
            const double gUp = fadeGain (t, p[1].fadeInStart, p[1].fadeInEnd, true);
            expectWithinAbsoluteError (gLow * gLow + gUp * gUp, 1.0, 1e-9);
        }

        beginTest ("loop takes: only the top take plays; muted takes are ignored");
        {
            std::vector<ClipSpan> takes { span (2, 4, 0, 0, 0, 0), span (2, 4, 4, 0, 0, 1), span (2, 4, 8, 0, 0, 2) };
            auto p = visiblePieces (takes);
            expectEquals ((int) p.size(), 1);
            expectEquals (p[0].clip, 2);
            takes[2].muted = true;
            p = visiblePieces (takes);
            expectEquals ((int) p.size(), 1);
            expectEquals (p[0].clip, 1);
            expectEquals (p[0].srcAtStart, 4.0);
        }

        beginTest ("non-overlapping clips are untouched and sorted");
        {
            auto p = visiblePieces ({ span (5, 1, 0, 0, 0, 0), span (0, 1, 0, 0, 0, 1) });
            expectEquals ((int) p.size(), 2);
            expectEquals (p[0].clip, 1);
            expectEquals (p[1].clip, 0);
        }

        beginTest ("model helpers & song end");
        {
            auto tracks = juce::ValueTree (ff::project::ids::TRACKS);
            auto t = createTrack ("Gtr", 0);
            t.appendChild (createClip ("Audio/a.wav", "a", 1.0, 2.0, 0.0), nullptr);
            auto muted = createClip ("Audio/b.wav", "b", 10.0, 2.0, 0.0);
            muted.setProperty (ids::muted, true, nullptr);
            t.appendChild (muted, nullptr);
            tracks.appendChild (t, nullptr);
            expectEquals (songEnd (tracks), 3.0);
            expect (t[ids::id].toString().length() > 10);
        }
    }
};
static TimelineTests timelineTests;
