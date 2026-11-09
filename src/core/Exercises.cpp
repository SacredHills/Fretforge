#include "Exercises.h"
#include "TabModel.h"
#include <algorithm>

namespace ff::training
{
namespace
{
constexpr int minorIv[] { 0, 2, 3, 5, 7, 8, 10 };
constexpr int majorIv[] { 0, 2, 4, 5, 7, 9, 11 };
constexpr int pentIv[] { 0, 3, 5, 7, 10 };

using Note = Step::Note;
using Steps = std::vector<Step>;

int strings (const Settings& s) { return (int) s.tuning.size(); }
int open (const Settings& s, int str) { return s.tuning[(size_t) juce::jlimit (0, strings (s) - 1, str)]; }
int rootNote (const Settings& s) { return open (s, 0) + juce::jlimit (0, 17, s.position); }

/** Ascending notes of a scale from `root`. */
std::vector<int> scale (int root, const int* iv, int n, int count)
{
    std::vector<int> out;
    for (int i = 0; (int) out.size() < count; ++i) out.push_back (root + 12 * (i / n) + iv[i % n]);
    return out;
}

/** `nps` notes per string, lowest string first: each note on the string it falls on (frets stay near the position). */
std::vector<Note> fingering (const Settings& s, const std::vector<int>& notes, int nps)
{
    std::vector<Note> out;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const int str = (int) i / nps;
        if (str >= strings (s)) break;
        int fret = notes[i] - open (s, str);
        while (fret < 0) fret += 12;
        while (fret > 24) fret -= 12;
        out.push_back ({ str, fret });
    }
    return out;
}

Step one (Note n) { Step st; st.notes.push_back (n); return st; }
Step rest() { return {}; }
Step chord (std::initializer_list<Note> ns) { Step st; st.notes = ns; return st; }

/** Up the run and back down (the top note once). */
Steps upAndDown (const std::vector<Note>& run)
{
    Steps out;
    for (auto& n : run) out.push_back (one (n));
    for (int i = (int) run.size() - 2; i > 0; --i) out.push_back (one (run[(size_t) i]));
    return out;
}

/** Runs a line in groups of 4 ("1234 2345 3456 ..."), up and back. */
Steps inFours (const std::vector<Note>& run)
{
    Steps out;
    for (size_t i = 0; i + 4 <= run.size(); ++i)
        for (size_t k = 0; k < 4; ++k) out.push_back (one (run[i + k]));
    for (size_t i = run.size(); i >= 4; --i)
        for (size_t k = 0; k < 4; ++k) out.push_back (one (run[i - 1 - k]));
    return out;
}

std::array<int, 4> permutationOf1234 (juce::Random& r, bool avoidAdjacent)
{
    std::array<int, 4> p { 0, 1, 2, 3 };
    for (int tries = 0; tries < 50; ++tries)
    {
        for (int i = 3; i > 0; --i) std::swap (p[(size_t) i], p[(size_t) r.nextInt (i + 1)]);
        if (! avoidAdjacent) break;
        bool ok = true;
        for (int i = 0; i < 3; ++i) if (std::abs (p[(size_t) i] - p[(size_t) i + 1]) == 1) ok = false;
        if (ok) break;
    }
    return p;
}

Steps spider (const Settings& s, juce::Random& r)
{
    const auto perm = s.seed <= 1 ? std::array<int, 4> { 0, 1, 2, 3 } : permutationOf1234 (r, false);
    Steps out;
    for (int str = 0; str < strings (s); ++str)
        for (int k : perm) out.push_back (one ({ str, s.position + k }));
    for (int str = strings (s) - 1; str >= 0; --str)
        for (int i = 3; i >= 0; --i) out.push_back (one ({ str, s.position + perm[(size_t) i] }));
    return out;
}

Steps independence (const Settings& s, juce::Random& r)
{
    const auto perm = s.seed <= 1 ? std::array<int, 4> { 0, 2, 1, 3 } : permutationOf1234 (r, true);
    Steps out;
    for (int str = 0; str < strings (s); ++str)
        for (int k : perm) out.push_back (one ({ str, s.position + k }));
    for (int str = strings (s) - 1; str >= 0; --str)
        for (int k : perm) out.push_back (one ({ str, s.position + 3 - k }));
    return out;
}

Steps stretches (const Settings& s, juce::Random& r)
{
    const int span = 4 + (s.seed > 1 ? r.nextInt (2) : 1);   // 1-3-5 or wider
    Steps out;
    for (int str = 0; str < strings (s); ++str)
        for (int k : { 0, 2, span - 1, span }) out.push_back (one ({ str, s.position + k }));
    for (int str = strings (s) - 1; str >= 0; --str)
        for (int k : { span, span - 1, 2, 0 }) out.push_back (one ({ str, s.position + k }));
    return out;
}

Steps threeNps (const Settings& s, juce::Random& r, const int* iv, bool legato, bool groupsOfFour)
{
    auto run = fingering (s, scale (rootNote (s), iv, 7, 3 * strings (s)), 3);
    if (legato)
        for (size_t i = 0; i < run.size(); ++i) run[i].hammer = i % 3 != 0;   // pick the first note on each string
    if (groupsOfFour || (! legato && s.seed > 1 && r.nextBool())) return inFours (run);
    return upAndDown (run);
}

// ---- minor pentatonic: the five shapes (CAGED boxes) ----
// Per shape and string (low E first, standard tuning): two frets relative to the root's fret on the low E string,
// and their degrees. Shape 1 has the root on the low E; each next shape starts where the last one ends.
struct ShapeString { int fret[2]; const char* degree[2]; };
constexpr ShapeString pentShapes[5][6] {
    { { { 0, 3 }, { "1", "b3" } }, { { 0, 2 }, { "4", "5" } }, { { 0, 2 }, { "b7", "1" } },
      { { 0, 2 }, { "b3", "4" } }, { { 0, 3 }, { "5", "b7" } }, { { 0, 3 }, { "1", "b3" } } },
    { { { 3, 5 }, { "b3", "4" } }, { { 2, 5 }, { "5", "b7" } }, { { 2, 5 }, { "1", "b3" } },
      { { 2, 4 }, { "4", "5" } }, { { 3, 5 }, { "b7", "1" } }, { { 3, 5 }, { "b3", "4" } } },
    { { { 5, 7 }, { "4", "5" } }, { { 5, 7 }, { "b7", "1" } }, { { 5, 7 }, { "b3", "4" } },
      { { 4, 7 }, { "5", "b7" } }, { { 5, 8 }, { "1", "b3" } }, { { 5, 7 }, { "4", "5" } } },
    { { { 7, 10 }, { "5", "b7" } }, { { 7, 10 }, { "1", "b3" } }, { { 7, 9 }, { "4", "5" } },
      { { 7, 9 }, { "b7", "1" } }, { { 8, 10 }, { "b3", "4" } }, { { 7, 10 }, { "5", "b7" } } },
    { { { 10, 12 }, { "b7", "1" } }, { { 10, 12 }, { "b3", "4" } }, { { 9, 12 }, { "5", "b7" } },
      { { 9, 12 }, { "1", "b3" } }, { { 10, 12 }, { "4", "5" } }, { { 10, 12 }, { "b7", "1" } } },
};
constexpr int standardOpen[6] { 40, 45, 50, 55, 59, 64 };

/** The notes of pentatonic shape `shape` (0..4) in the key of the position, on the top six strings of the tuning,
    lowest string first, low to high on each string. Moved down an octave when it would go past fret 22. */
std::vector<Note> pentShape (const Settings& s, int shape)
{
    const int n = strings (s), first = juce::jmax (0, n - 6);
    const int key = 40 + juce::jlimit (0, 17, s.position);   // the root on the low E string of standard tuning
    auto build = [&] (int shift) {
        std::vector<Note> out;
        for (int k = 0; k < 6 && first + k < n; ++k)
            for (int j = 0; j < 2; ++j)
            {
                const int pitch = standardOpen[k] + key - 40 + pentShapes[shape][k].fret[j] + shift;
                Note nt { first + k, pitch - open (s, first + k) };
                nt.degree = pentShapes[shape][k].degree[j];
                out.push_back (nt);
            }
        return out;
    };
    auto out = build (0);
    int lo = 99, hi = -99;
    for (auto& nt : out) { lo = juce::jmin (lo, nt.fret); hi = juce::jmax (hi, nt.fret); }
    if (hi > 22 && lo >= 12) out = build (-12);
    else if (lo < 0) out = build (12);
    return out;
}

/** Up and down a shape; variations: in groups of four, in threes, or each string's pair twice. */
Steps pentShapeRun (const Settings& s, juce::Random& r, int shape)
{
    auto run = pentShape (s, shape);
    if (s.seed <= 1) return upAndDown (run);
    switch (r.nextInt (3))
    {
        case 0: return inFours (run);
        case 1:
        {
            Steps out;   // groups of three up, then down
            for (size_t i = 0; i + 3 <= run.size(); ++i) for (size_t k = 0; k < 3; ++k) out.push_back (one (run[i + k]));
            for (size_t i = run.size(); i >= 3; --i) for (size_t k = 0; k < 3; ++k) out.push_back (one (run[i - 1 - k]));
            return out;
        }
        default:
        {
            Steps out;   // each string's two notes twice, up then down
            for (size_t i = 0; i + 1 < run.size(); i += 2) for (int rep = 0; rep < 2; ++rep) { out.push_back (one (run[i])); out.push_back (one (run[i + 1])); }
            for (size_t i = run.size(); i >= 2; i -= 2) for (int rep = 0; rep < 2; ++rep) { out.push_back (one (run[i - 1])); out.push_back (one (run[i - 2])); }
            return out;
        }
    }
}

Steps pentatonicBox (const Settings& s, juce::Random& r) { return pentShapeRun (s, r, 0); }
Steps pentShape2 (const Settings& s, juce::Random& r) { return pentShapeRun (s, r, 1); }
Steps pentShape3 (const Settings& s, juce::Random& r) { return pentShapeRun (s, r, 2); }
Steps pentShape4 (const Settings& s, juce::Random& r) { return pentShapeRun (s, r, 3); }
Steps pentShape5 (const Settings& s, juce::Random& r) { return pentShapeRun (s, r, 4); }

Steps stringSkipping (const Settings& s, juce::Random& r)
{
    auto run = fingering (s, scale (rootNote (s), pentIv, 5, 2 * strings (s)), 2);
    // pairs of strings two apart: 1-3, 2-4, 3-5 ... (two notes each)
    Steps out;
    const int jump = s.seed > 1 && r.nextBool() ? 3 : 2;
    for (int a = 0; a + jump < strings (s); ++a)
        for (int str : { a, a + jump })
            for (int k = 0; k < 2; ++k) out.push_back (one (run[(size_t) (str * 2 + k)]));
    return out;
}

/** Notes of a chord (intervals from the root) one per string, from the 5th string up, near the position. */
std::vector<Note> arpeggioShape (const Settings& s, std::initializer_list<int> iv, int fromString, int perString)
{
    std::vector<Note> out;
    std::vector<int> tones;
    const int root = rootNote (s) + (fromString > 0 ? 0 : 0);
    for (int oct = 0; oct < 4; ++oct) for (int i : iv) tones.push_back (root + 12 * oct + i);
    int prev = root - 1;
    for (int str = fromString; str < strings (s); ++str)
        for (int k = 0; k < perString; ++k)
            for (int t : tones)
                if (t > prev)
                {
                    const int fret = t - open (s, str);
                    if (fret >= s.position - 2 && fret <= s.position + 5) { out.push_back ({ str, fret }); prev = t; break; }
                    if (fret > s.position + 5) break;
                }
    return out;
}

Steps sweep (const Settings& s, juce::Random& r)
{
    const int type = s.seed > 1 ? r.nextInt (3) : 0;
    auto shape = arpeggioShape (s, type == 0 ? std::initializer_list<int> { 0, 3, 7 } : type == 1 ? std::initializer_list<int> { 0, 4, 7 }
                                                                                                 : std::initializer_list<int> { 0, 3, 6 },
                                0, 1);
    return upAndDown (shape);
}

Steps arpeggio7 (const Settings& s, juce::Random& r)
{
    const bool major = s.seed > 1 && r.nextBool();
    auto shape = arpeggioShape (s, major ? std::initializer_list<int> { 0, 4, 7, 11 } : std::initializer_list<int> { 0, 3, 7, 10 }, 0, 2);
    return upAndDown (shape);
}

Steps economy (const Settings& s, juce::Random& r)
{
    // three notes on a string then one string over: the pick keeps going the same way across the strings
    auto run = fingering (s, scale (rootNote (s), minorIv, 7, 3 * strings (s)), 3);
    juce::ignoreUnused (r);
    return upAndDown (run);
}

Steps insideOutside (const Settings& s, juce::Random& r)
{
    const int a = juce::jlimit (0, strings (s) - 2, 2 + (s.seed > 1 ? r.nextInt (3) - 1 : 0));
    Steps out;
    // inside: the two notes are picked between the strings; outside: from outside them
    for (int rep = 0; rep < 2; ++rep)
    {
        out.push_back (one ({ a + 1, s.position + 2 }));
        out.push_back (one ({ a, s.position + 4 }));
        out.push_back (one ({ a + 1, s.position + 2 }));
        out.push_back (one ({ a, s.position + 5 }));
    }
    for (int rep = 0; rep < 2; ++rep)
    {
        out.push_back (one ({ a, s.position + 4 }));
        out.push_back (one ({ a + 1, s.position + 2 }));
        out.push_back (one ({ a, s.position + 5 }));
        out.push_back (one ({ a + 1, s.position + 3 }));
    }
    return out;
}

Steps tremolo (const Settings& s, juce::Random& r)
{
    auto notes = scale (rootNote (s) + 12, minorIv, 7, 8);
    Steps out;
    const int str = juce::jmin (3, strings (s) - 1);
    for (int i = 0; i < 4; ++i)
    {
        const int deg = s.seed > 1 ? r.nextInt (6) : i;
        int fret = notes[(size_t) deg] - open (s, str);
        while (fret < 0) fret += 12;
        while (fret > 22) fret -= 12;
        for (int k = 0; k < 4; ++k) out.push_back (one ({ str, fret }));
    }
    return out;
}

Note pm (int str, int fret, bool accent = false)
{
    Note n { str, fret };
    n.palmMute = ! accent;
    n.accent = accent;
    return n;
}

Steps gallop (const Settings& s, juce::Random& r)
{
    Steps out;
    for (int beat = 0; beat < 4; ++beat)
    {
        const bool hit = s.seed > 1 ? r.nextInt (4) == 0 : beat == 3;
        if (hit) out.push_back (chord ({ { 0, s.position, false, false, false, false, true }, { 1, s.position + 2, false, false, false, false, true } }));
        else out.push_back (one (pm (0, 0)));
        out.push_back (rest());
        out.push_back (one (pm (0, 0)));
        out.push_back (one (pm (0, 0)));
    }
    return out;
}

Steps chugs (const Settings& s, juce::Random& r)
{
    Steps out;
    for (int i = 0; i < 16; ++i)
    {
        const bool hit = (s.seed > 1 ? r.nextInt (5) == 0 : (i == 3 || i == 6 || i == 10)) && i % 4 != 0;
        if (hit) out.push_back (chord ({ pm (0, s.position, true), pm (1, s.position + 2, true) }));
        else out.push_back (one (pm (0, 0)));
    }
    return out;
}

Steps downpicking (const Settings& s, juce::Random& r)
{
    Steps out;
    const int degrees[] { 0, 0, 3, 0, 5, 0, 3, 1 };
    for (int i = 0; i < 8; ++i)
    {
        const int d = s.seed > 1 ? (r.nextInt (3) == 0 ? r.nextInt (6) : 0) : degrees[i];
        const int fret = d == 0 ? 0 : s.position + minorIv[d % 7] - 0;
        out.push_back (d == 0 ? one (pm (0, 0)) : chord ({ { 0, fret }, { 1, fret + 2 } }));
    }
    return out;
}

Steps powerChords (const Settings& s, juce::Random& r)
{
    Steps out;
    const int roots[] { 0, 3, 5, 7, 10 };
    for (int c = 0; c < 4; ++c)
    {
        const int iv = s.seed > 1 ? roots[r.nextInt (5)] : roots[c % 4 == 3 ? 2 : c];
        const int str = (s.position + iv) > 10 ? 1 : 0;
        const int fret = s.position + iv - (str == 1 ? 5 : 0);
        for (int k = 0; k < 4; ++k) out.push_back (chord ({ { str, fret }, { str + 1, fret + 2 }, { str + 2, fret + 2 } }));
    }
    return out;
}

Steps syncopation (const Settings& s, juce::Random& r)
{
    bool hits[16] { true, false, false, true, false, false, true, false, false, false, true, false, true, false, false, false };
    if (s.seed > 1)
    {
        for (auto& h : hits) h = r.nextInt (3) == 0;
        hits[0] = true;
    }
    Steps out;
    for (int i = 0; i < 16; ++i)
        out.push_back (hits[i] ? chord ({ { 0, s.position, false, false, false, false, i % 4 != 0 }, { 1, s.position + 2 } }) : rest());
    return out;
}

Steps trills (const Settings& s, juce::Random& r)
{
    Steps out;
    const int gap = s.seed > 1 ? 1 + r.nextInt (3) : 2;
    for (int str = strings (s) - 1; str >= 0; str -= 2)
        for (int k = 0; k < 8; ++k)
        {
            Note n { str, s.position + (k % 2 == 0 ? 0 : gap) };
            n.hammer = k > 0;
            out.push_back (one (n));
        }
    return out;
}

Steps tapping (const Settings& s, juce::Random& r)
{
    Steps out;
    const int str = strings (s) - 1;
    const int high = s.position + 12, low = s.position, mid = s.position + (s.seed > 1 ? 3 + r.nextInt (2) : 3);
    for (int k = 0; k < 4; ++k)
    {
        Note t { str, high }; t.tap = true;
        Note a { str, low }; a.hammer = true;
        Note b { str, mid }; b.hammer = true;
        out.push_back (one (t)); out.push_back (one (a)); out.push_back (one (b));
    }
    return out;
}

Steps shifts (const Settings& s, juce::Random& r)
{
    Steps out;
    const int str = juce::jmin (2, strings (s) - 1);
    auto notes = scale (open (s, str) + juce::jmin (s.position, 10), minorIv, 7, 12);
    for (int pos = 0; pos < 4; ++pos)
    {
        const int base = notes[(size_t) (pos * 2)] - open (s, str);
        for (int k = 0; k < 3; ++k)
        {
            Note n { str, juce::jlimit (0, 22, base + minorIv[(size_t) k] - minorIv[0]) };
            n.slide = k == 2;   // slide into the next position
            out.push_back (one (n));
        }
        out.push_back (rest());
    }
    juce::ignoreUnused (r);
    return out;
}

Steps bends (const Settings& s, juce::Random& r)
{
    Steps out;
    const int str = juce::jlimit (0, strings (s) - 1, strings (s) - 2);   // B string
    for (int k = 0; k < 2; ++k)
    {
        const int fret = s.position + (s.seed > 1 ? r.nextInt (3) * 2 + 3 : 7);
        Note b { str, fret }; b.bend = (k == 0 ? 1.0f : 0.5f);
        out.push_back (one (b));
        for (int i = 0; i < 3; ++i) out.push_back (rest());
        out.push_back (one ({ str, fret + (k == 0 ? 2 : 1) }));   // the target pitch, fretted: does the bend match?
        for (int i = 0; i < 3; ++i) out.push_back (rest());
    }
    return out;
}

Steps vibrato (const Settings& s, juce::Random& r)
{
    Steps out;
    auto notes = scale (rootNote (s) + 12, pentIv, 5, 5);
    for (int k = 0; k < 4; ++k)
    {
        const int str = juce::jlimit (0, strings (s) - 1, 2 + (s.seed > 1 ? r.nextInt (3) : k % 3));
        int fret = notes[(size_t) (k % 5)] - open (s, str);
        while (fret < 0) fret += 12;
        while (fret > 22) fret -= 12;
        Note n { str, fret }; n.vibrato = true;
        out.push_back (one (n));
        for (int i = 0; i < 3; ++i) out.push_back (rest());
    }
    return out;
}

Steps alternate (const Settings& s, juce::Random& r) { return threeNps (s, r, minorIv, false, s.seed > 1 && r.nextBool()); }
Steps legato (const Settings& s, juce::Random& r) { return threeNps (s, r, minorIv, true, false); }
Steps major3 (const Settings& s, juce::Random& r) { return threeNps (s, r, majorIv, false, false); }

using Generator = Steps (*) (const Settings&, juce::Random&);
struct Entry { Technique t; Generator g; };

const std::vector<Entry>& entries()
{
    static const std::vector<Entry> e {
        { { "spider", "Chromatic spider (1-2-3-4)", "Fretting hand", "One finger per fret; keep the fingers close to the strings and the thumb behind the neck.", 60, 160 }, spider },
        { { "independence", "Finger independence", "Fretting hand", "Non-adjacent finger orders: only the finger that plays moves.", 50, 140 }, independence },
        { { "stretches", "Stretches", "Fretting hand", "Relax the hand and let the wrist drop; stop if it hurts.", 50, 120 }, stretches },
        { { "shifts", "Position shifts", "Fretting hand", "Slide into each new position on time; the shift is part of the beat.", 60, 140 }, shifts },
        { { "alternate", "Alternate picking", "Picking", "Strict down-up; small pick movements from the wrist.", 70, 200 }, alternate },
        { { "economy", "Economy picking", "Picking", "Keep the pick moving the same way when you change string.", 70, 200 }, economy },
        { { "insideout", "Inside / outside picking", "Picking", "Cross between two strings cleanly: inside (between) and outside (around).", 60, 180 }, insideOutside },
        { { "stringskip", "String skipping", "Picking", "Mute the skipped string; aim the pick, don't look for it.", 60, 170 }, stringSkipping },
        { { "tremolo", "Tremolo picking", "Picking", "Even, fast strokes; tense the forearm as little as you can.", 80, 220 }, tremolo },
        { { "sweep", "Sweep picking", "Picking", "One smooth stroke per direction; each note sounds alone (roll / lift fingers).", 50, 160 }, sweep },
        { { "hammer", "Hammer-ons & pull-offs", "Legato", "Hammer from the fingertip; pull off slightly down so the string rings.", 60, 180 }, trills },
        { { "legato", "Legato runs (3 notes per string)", "Legato", "Pick only the first note on each string; all notes the same volume.", 60, 190 }, legato },
        { { "tapping", "Two-hand tapping", "Legato", "Tap with the picking hand, pull off sideways; mute the other strings.", 60, 170 }, tapping },
        { { "gallop", "Gallop rhythm", "Rhythm", "Down-down-up (or down-up-down) palm-muted gallops, accents on the chords.", 80, 200 }, gallop },
        { { "chugs", "Palm-muted chugs", "Rhythm", "Palm on the bridge; accented chords stay tight and short.", 80, 200 }, chugs },
        { { "downpick", "Downpicking endurance", "Rhythm", "All downstrokes; let the wrist do the work and stay relaxed.", 100, 220 }, downpicking },
        { { "powerchords", "Power chord changes", "Rhythm", "Move the whole shape at once; land on the beat.", 60, 160 }, powerChords },
        { { "syncopation", "Syncopation", "Rhythm", "Count the 16ths; the rests are as important as the hits.", 60, 160 }, syncopation },
        { { "bends", "Bends to pitch", "Expression", "Bend with several fingers; check the bend against the fretted target note.", 50, 110 }, bends },
        { { "vibrato", "Vibrato", "Expression", "Even width and speed, from the wrist; keep it in time with the tempo.", 50, 110 }, vibrato },
        { { "pentatonic", "Minor pentatonic shape 1", "Scales & arpeggios", "Root on the low E (the classic box). Two notes per string; say the root notes as you play them. Position sets the key.", 70, 200 }, pentatonicBox },
        { { "pent2", "Minor pentatonic shape 2", "Scales & arpeggios", "Starts where shape 1 ends; the root is on the D and B strings.", 70, 200 }, pentShape2 },
        { { "pent3", "Minor pentatonic shape 3", "Scales & arpeggios", "Root on the A and B strings; note the 4-fret stretch on the B string.", 70, 200 }, pentShape3 },
        { { "pent4", "Minor pentatonic shape 4", "Scales & arpeggios", "Root on the A and G strings.", 70, 200 }, pentShape4 },
        { { "pent5", "Minor pentatonic shape 5", "Scales & arpeggios", "Root on both E strings and the G string; it joins back to shape 1 an octave up.", 70, 200 }, pentShape5 },
        { { "major3nps", "Major scale (3 notes per string)", "Scales & arpeggios", "Three notes per string up and down; know the root notes.", 60, 190 }, major3 },
        { { "arp7", "Seventh-chord arpeggios", "Scales & arpeggios", "Two notes per string; let each note ring separately.", 50, 160 }, arpeggio7 },
    };
    return e;
}
} // namespace

const std::vector<Technique>& techniques()
{
    static const std::vector<Technique> t = [] {
        std::vector<Technique> v;
        for (auto& e : entries()) v.push_back (e.t);
        return v;
    }();
    return t;
}

int techniqueIndex (const juce::String& id)
{
    for (size_t i = 0; i < techniques().size(); ++i)
        if (id == techniques()[i].id) return (int) i;
    return -1;
}

std::vector<Step::Note> scaleDiagram (const Settings& s)
{
    if (s.tuning.size() < 6) return {};
    const juce::String id = techniques()[(size_t) juce::jlimit (0, (int) techniques().size() - 1, s.technique)].id;
    const int shape = id == "pentatonic" ? 0 : id == "pent2" ? 1 : id == "pent3" ? 2 : id == "pent4" ? 3 : id == "pent5" ? 4 : -1;
    return shape < 0 ? std::vector<Step::Note>() : pentShape (s, shape);
}

std::vector<Step> pattern (const Settings& s)
{
    if (s.tuning.size() < 4) return {};
    const auto& e = entries()[(size_t) juce::jlimit (0, (int) entries().size() - 1, s.technique)];
    juce::Random r ((juce::int64) s.seed * 7919 + s.technique);
    auto steps = e.g (s, r);
    for (auto& st : steps)
        for (auto& n : st.notes)
        {
            n.string = juce::jlimit (0, strings (s) - 1, n.string);
            n.fret = juce::jlimit (0, tab::maxFret, n.fret);
        }
    return steps;
}

juce::ValueTree buildScore (const Settings& s, double bpm)
{
    juce::ValueTree project ("PROJECT");
    auto score = tab::getOrCreateScore (project);
    score.setProperty (tab::ids::title, techniques()[(size_t) juce::jlimit (0, (int) techniques().size() - 1, s.technique)].name, nullptr);
    score.setProperty (tab::ids::tempo, juce::jlimit (10.0, 999.0, bpm), nullptr);
    tab::measures (score).removeAllChildren (nullptr);   // a fresh score has 4 empty bars and no tracks yet
    const int bars = juce::jlimit (1, 16, s.bars);
    for (int i = 0; i < bars; ++i) tab::addMeasure (score, -1, 4, 4, nullptr);
    auto track = tab::addTrack (score, "Exercise", s.tuning, "guitar", nullptr);

    const auto steps = pattern (s);
    const int perBar = s.subdivision == 8 ? 8 : s.subdivision == 12 ? 12 : 16;
    const int dur = s.subdivision == 16 ? 16 : 8;
    size_t k = 0;
    for (int b = 0; b < bars; ++b)
    {
        auto bar = tab::bar (track, b);
        for (int i = 0; i < perBar; ++i)
        {
            const Step st = steps.empty() ? Step{} : steps[k++ % steps.size()];
            auto beat = tab::makeBeat (dur, st.notes.empty());
            if (s.subdivision == 12) beat.setProperty (tab::ids::tuplet, 3, nullptr);
            for (auto& n : st.notes)
            {
                auto note = tab::makeNote (n.string, n.fret);
                if (n.hammer) note.setProperty (tab::ids::hammer, true, nullptr);
                if (n.palmMute) note.setProperty (tab::ids::palmMute, true, nullptr);
                if (n.vibrato) note.setProperty (tab::ids::vibrato, true, nullptr);
                if (n.tap) note.setProperty (tab::ids::tap, true, nullptr);
                if (n.accent) note.setProperty (tab::ids::accent, true, nullptr);
                if (n.slide) note.setProperty (tab::ids::slide, (int) tab::slideTo, nullptr);
                if (n.bend > 0.0f) note.setProperty (tab::ids::bend, (double) n.bend, nullptr);
                beat.appendChild (note, nullptr);
            }
            bar.appendChild (beat, nullptr);
        }
    }
    project.removeChild (score, nullptr);
    return score;
}

juce::String asciiTab (const Settings& s)
{
    const auto steps = pattern (s);
    const char* names[] { "E", "A", "D", "G", "B", "e", "B", "F#" };
    juce::String out;
    for (int str = strings (s) - 1; str >= 0; --str)
    {
        juce::String line = juce::String (str < 8 ? names[str] : "?") + "|";
        for (auto& st : steps)
        {
            juce::String cell = "-";
            for (auto& n : st.notes)
                if (n.string == str)
                    cell = juce::String (n.fret) + (n.hammer ? "h" : n.tap ? "t" : n.bend > 0.0f ? "b" : n.vibrato ? "~" : n.slide ? "/" : "");
            line << cell.paddedRight ('-', 4);
        }
        out << line << "|\n";
    }
    return out;
}

const std::vector<Plan>& plans()
{
    static const std::vector<Plan> p {
        { "Fundamentals", "Daily basics for clean, even playing.", { "spider", "alternate", "hammer", "powerchords", "pentatonic" } },
        { "Speed picking", "Builds clean, fast picking step by step.", { "alternate", "insideout", "stringskip", "economy", "tremolo" } },
        { "Metal rhythm", "Tight right hand for riffs.", { "downpick", "chugs", "gallop", "powerchords", "syncopation" } },
        { "Lead & shred", "Lead techniques for solos.", { "legato", "sweep", "tapping", "major3nps", "arp7" } },
        { "Pentatonic shapes", "All five minor pentatonic shapes in one key, so the whole neck connects.", { "pentatonic", "pent2", "pent3", "pent4", "pent5" } },
        { "Expression", "Make single notes sing.", { "bends", "vibrato", "shifts", "stretches", "independence" } },
    };
    return p;
}
} // namespace ff::training
