#include "DrumModel.h"
#include "Timeline.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace ff::drums
{
namespace
{
const PieceInfo pieces[numPieces] = {
    { "Kick", 36, 0, 0.0f },
    { "Kick 2", 35, 0, 0.0f },
    { "Snare", 38, 0, 0.0f },
    { "Side stick", 37, 0, 0.0f },
    { "Clap", 39, 0, 0.0f },
    { "Hi-hat closed", 42, 1, -0.35f },
    { "Hi-hat pedal", 44, 1, -0.35f },
    { "Hi-hat open", 46, 1, -0.35f },
    { "Tom high", 50, 0, -0.25f },
    { "Tom mid", 48, 0, 0.0f },
    { "Tom low", 45, 0, 0.25f },
    { "Floor tom", 41, 0, 0.45f },
    { "Crash", 49, 0, -0.4f },
    { "Crash 2", 57, 0, 0.4f },
    { "China", 52, 0, 0.5f },
    { "Splash", 55, 0, -0.2f },
    { "Ride", 51, 0, 0.45f },
    { "Ride bell", 53, 0, 0.45f },
    { "Cowbell", 56, 0, 0.1f },
};
} // namespace

const PieceInfo& pieceInfo (int p) { return pieces[juce::jlimit (0, (int) numPieces - 1, p)]; }

int pieceForGmNote (int note)
{
    for (int i = 0; i < numPieces; ++i)
        if (pieces[i].gmNote == note) return i;
    switch (note)
    {
        case 40: return snare;      // electric snare
        case 47: return tomMid;     // low-mid tom
        case 43: return tomFloor;   // high floor tom
        case 59: return ride;       // ride 2
        case 58: return cowbell;
        case 21: return hhPedal;    // extended kits (e.g. Guitar Pro / EZdrummer style hi-hat articulations)
        case 22: return hhClosed;
        case 23: case 24: case 25: case 26: return hhOpen;
        default: return -1;
    }
}

// ------------------------------------------------------------------ structure

juce::ValueTree find (const juce::ValueTree& project) { return project.getChildWithName (ids::DRUMS); }
juce::ValueTree patterns (const juce::ValueTree& d) { return d.getChildWithName (ids::PATTERNS); }
juce::ValueTree song (const juce::ValueTree& d) { return d.getChildWithName (ids::SONG); }

juce::ValueTree getOrCreate (juce::ValueTree project)
{
    auto d = find (project);
    if (! d.isValid())
    {
        d = juce::ValueTree (ids::DRUMS);
        d.setProperty (ids::humanizeMs, 0.0, nullptr);
        d.setProperty (ids::humanizeVel, 0.0, nullptr);
        d.setProperty (ids::volume, 0.8, nullptr);
        project.appendChild (d, nullptr);
    }
    if (! patterns (d).isValid()) d.appendChild (juce::ValueTree (ids::PATTERNS), nullptr);
    if (! song (d).isValid()) d.appendChild (juce::ValueTree (ids::SONG), nullptr);
    return d;
}

juce::ValueTree patternById (const juce::ValueTree& d, const juce::String& id)
{
    for (auto p : patterns (d))
        if (p[ids::id].toString() == id) return p;
    return {};
}

juce::ValueTree makePattern (const juce::String& name, int tsNum, int tsDen, int bars, int grid)
{
    juce::ValueTree p (ids::PATTERN);
    p.setProperty (ids::id, timeline::newId(), nullptr);
    p.setProperty (ids::name, name, nullptr);
    p.setProperty (ids::tsNum, juce::jlimit (1, 32, tsNum), nullptr);
    p.setProperty (ids::tsDen, juce::jlimit (1, 32, tsDen), nullptr);
    p.setProperty (ids::bars, juce::jlimit (1, maxBars, bars), nullptr);
    p.setProperty (ids::grid, juce::jlimit (1, 16, grid), nullptr);
    p.setProperty (ids::swing, 0.0, nullptr);
    return p;
}

juce::ValueTree addPattern (juce::ValueTree d, const juce::String& name, int tsNum, int tsDen, int bars, juce::UndoManager* um)
{
    auto p = makePattern (name, tsNum, tsDen, bars, 4);
    patterns (d).appendChild (p, um);
    return p;
}

juce::ValueTree addSection (juce::ValueTree d, const juce::String& patternId, int repeats, int index, juce::UndoManager* um)
{
    juce::ValueTree s (ids::SECTION);
    s.setProperty (ids::pattern, patternId, nullptr);
    s.setProperty (ids::repeats, juce::jlimit (1, 64, repeats), nullptr);
    song (d).addChild (s, index, um);
    return s;
}

// ------------------------------------------------------------------ sample kit

namespace
{
juce::Identifier sampleId (int piece) { return juce::Identifier ("sample" + juce::String (piece)); }
}

juce::String samplePath (const juce::ValueTree& d, int piece)
{
    return d.getChildWithName (ids::KIT)[sampleId (piece)].toString();
}

void setSamplePath (juce::ValueTree d, int piece, const juce::String& rel, juce::UndoManager* um)
{
    auto kit = d.getChildWithName (ids::KIT);
    if (! kit.isValid())
    {
        kit = juce::ValueTree (ids::KIT);
        d.appendChild (kit, um);
    }
    if (rel.isEmpty()) kit.removeProperty (sampleId (piece), um);
    else kit.setProperty (sampleId (piece), rel, um);
}

int pieceForSampleName (const juce::String& fileName)
{
    const auto n = " " + juce::File::createFileWithoutCheckingPath (fileName).getFileNameWithoutExtension().toLowerCase()
                             .replaceCharacters ("_-.()[]", "       ") + " ";
    auto has = [&] (const char* w) { return n.contains (w); };
    auto word = [&] (const char* w) { return n.contains (juce::String (" ") + w + " "); };
    if (has ("ride") && has ("bell")) return rideBell;
    if (has ("ride")) return ride;
    if (has ("china")) return china;
    if (has ("splash")) return splash;
    if (has ("crash")) return (has ("crash2") || has ("crash 2") || word ("b") || has ("crash right")) ? crash2 : crash;
    if (has ("cowbell") || has ("cow bell")) return cowbell;
    if (has ("clap")) return clap;
    if (has ("side") || has ("xstick") || has ("cross") || word ("stick") || word ("rim")) return sideStick;
    if (has ("snare") || word ("snr") || word ("sd") || has ("rimshot")) return snare;
    if (has ("hat") || word ("hh") || has ("hihat"))
    {
        if (has ("open") || word ("opn") || word ("oh")) return hhOpen;
        if (has ("pedal") || has ("foot") || word ("ped") || word ("chick")) return hhPedal;
        return hhClosed;
    }
    if (has ("kick") || has ("kik") || word ("bd") || has ("bass drum") || has ("bassdrum"))
        return (word ("2") || word ("b")) ? kick2 : kick;
    if (has ("floor") || has ("ftom")) return tomFloor;
    if (has ("tom"))
    {
        if (has ("high") || word ("hi") || word ("1")) return tomHigh;
        if (has ("low") || word ("lo") || word ("3")) return tomLow;
        if (word ("4")) return tomFloor;
        return tomMid;
    }
    return -1;
}

// ------------------------------------------------------------------ pattern helpers

int barTicks (const juce::ValueTree& p)
{
    return (int) p.getProperty (ids::tsNum, 4) * ppq * 4 / juce::jmax (1, (int) p.getProperty (ids::tsDen, 4));
}

int lengthTicks (const juce::ValueTree& p) { return barTicks (p) * juce::jmax (1, (int) p.getProperty (ids::bars, 1)); }
int stepTicks (const juce::ValueTree& p) { return ppq / juce::jlimit (1, 16, (int) p.getProperty (ids::grid, 4)); }

juce::ValueTree hitAt (const juce::ValueTree& p, int lane, int tick, int tol)
{
    for (auto h : p)
        if ((int) h[ids::lane] == lane && std::abs ((int) h[ids::tick] - tick) <= tol) return h;
    return {};
}

void setHit (juce::ValueTree p, int lane, int tick, int vel, juce::UndoManager* um)
{
    vel = juce::jlimit (1, 127, vel);
    if (auto h = hitAt (p, lane, tick); h.isValid())
    {
        h.setProperty (ids::vel, vel, um);
        return;
    }
    juce::ValueTree h (ids::HIT);
    h.setProperty (ids::lane, lane, nullptr);
    h.setProperty (ids::tick, tick, nullptr);
    h.setProperty (ids::vel, vel, nullptr);
    p.appendChild (h, um);
}

void removeHit (juce::ValueTree p, int lane, int tick, juce::UndoManager* um)
{
    if (auto h = hitAt (p, lane, tick); h.isValid()) p.removeChild (h, um);
}

void setBars (juce::ValueTree p, int bars, juce::UndoManager* um)
{
    bars = juce::jlimit (1, maxBars, bars);
    p.setProperty (ids::bars, bars, um);
    const int len = lengthTicks (p);
    for (int i = p.getNumChildren(); --i >= 0;)
        if ((int) p.getChild (i)[ids::tick] >= len) p.removeChild (i, um);
}

void replaceHits (juce::ValueTree p, const juce::ValueTree& src, juce::UndoManager* um)
{
    p.removeAllChildren (um);
    for (auto h : src) p.appendChild (h.createCopy(), um);
}

int swungTick (int tick, double swing, int grid)
{
    if (swing <= 0.0) return tick;
    const int unit = grid >= 4 ? ppq / 4 : ppq / 2;          // swing 16ths or 8ths
    if (tick % unit != 0 || (tick / unit) % 2 == 0) return tick;
    return tick + juce::roundToInt (juce::jlimit (0.0, 0.5, swing) * unit);
}

// ------------------------------------------------------------------ compile

namespace
{
void addPatternEvents (Compiled& c, const juce::ValueTree& d, const juce::ValueTree& p, double startQ, juce::uint32 salt)
{
    const double humMs = juce::jlimit (0.0, 50.0, (double) d.getProperty (ids::humanizeMs, 0.0));
    const double humVel = juce::jlimit (0.0, 1.0, (double) d.getProperty (ids::humanizeVel, 0.0));
    const double swing = (double) p.getProperty (ids::swing, 0.0);
    const int grid = (int) p.getProperty (ids::grid, 4);
    const int len = lengthTicks (p);
    int i = 0;
    for (auto h : p)
    {
        const int tick = (int) h[ids::tick];
        if (tick < 0 || tick >= len) continue;
        Event e;
        e.q = startQ + swungTick (tick, swing, grid) / (double) ppq;
        e.piece = juce::jlimit (0, (int) numPieces - 1, (int) h[ids::lane]);
        e.velocity = juce::jlimit (1, 127, (int) h.getProperty (ids::vel, 100)) / 127.0f;
        if (humMs > 0.0 || humVel > 0.0)
        {
            // deterministic per hit and repetition: the same groove plays the same way every time
            juce::Random r ((juce::int64) (salt * 2654435761u + (juce::uint32) tick * 40503u + (juce::uint32) e.piece * 9973u + 17u));
            const double t = (r.nextDouble() + r.nextDouble() - 1.0);   // triangular -1..1
            e.offsetSec = t * humMs / 1000.0;
            e.velocity = juce::jlimit (0.02f, 1.0f, e.velocity * (float) (1.0 + (r.nextDouble() * 2.0 - 1.0) * humVel));
        }
        c.events.push_back (e);
        ++i;
    }
    const int bars = (int) p.getProperty (ids::bars, 1);
    const double barQ = barTicks (p) / (double) ppq;
    for (int b = 0; b < bars; ++b)
        c.bars.push_back ({ startQ + b * barQ, (int) p.getProperty (ids::tsNum, 4), (int) p.getProperty (ids::tsDen, 4) });
}
} // namespace

Compiled compile (const juce::ValueTree& d, const juce::ValueTree& pattern, int times)
{
    Compiled c;
    juce::uint32 salt = 1;
    if (pattern.isValid())
    {
        for (int t = 0; t < juce::jmax (1, times); ++t)
        {
            addPatternEvents (c, d, pattern, c.lengthQ, salt++);
            c.lengthQ += lengthTicks (pattern) / (double) ppq;
        }
    }
    else
    {
        for (auto s : song (d))
        {
            auto p = patternById (d, s[ids::pattern].toString());
            if (! p.isValid()) continue;
            for (int r = 0; r < juce::jlimit (1, 64, (int) s.getProperty (ids::repeats, 1)); ++r)
            {
                addPatternEvents (c, d, p, c.lengthQ, salt++);
                c.lengthQ += lengthTicks (p) / (double) ppq;
            }
        }
    }
    std::stable_sort (c.events.begin(), c.events.end(), [] (const Event& a, const Event& b) { return a.q < b.q; });
    return c;
}

// ------------------------------------------------------------------ generator

const char* genreName (int g)
{
    static const char* names[numGenres] = { "Rock", "Hard rock", "Pop", "Punk", "Funk", "Blues shuffle", "Reggae one-drop", "Jazz swing",
                                            "Metal", "Thrash", "D-beat", "Doom", "Death metal", "Black metal", "Blast beats",
                                            "Double-bass gallop", "Djent / polyrhythm" };
    return names[juce::jlimit (0, (int) numGenres - 1, g)];
}

namespace
{
constexpr int Q = ppq, E8 = ppq / 2, S16 = ppq / 4, T8 = ppq / 3;

struct Gen
{
    juce::ValueTree p;
    juce::Random rng;
    double c = 0.5;
    int barLen = 0, beatLen = Q, beats = 4;
    bool compound = false;

    bool chance (double prob) { return rng.nextDouble() < prob; }
    int jitterVel (int v, int spread = 6) { return juce::jlimit (1, 127, v + rng.nextInt (spread * 2 + 1) - spread); }
    void hit (int lane, int tick, int vel)
    {
        if (tick < 0 || tick >= lengthTicks (p)) return;
        setHit (p, lane, tick, jitterVel (vel), nullptr);
    }
    void clear (int lane, int tick) { removeHit (p, lane, tick, nullptr); }
    void clearRange (int from, int to)
    {
        for (int i = p.getNumChildren(); --i >= 0;)
        {
            const int t = (int) p.getChild (i)[ids::tick];
            if (t >= from && t < to) p.removeChild (i, nullptr);
        }
    }
    /** Steady cymbal pulse with accents on the beats. */
    void pulse (int base, int lane, int step, int accent = 100, int soft = 78)
    {
        for (int t = 0; t < barLen; t += step)
            hit (lane, base + t, t % beatLen == 0 ? accent : soft);
    }
    std::vector<int> backbeats (bool halfTime) const
    {
        std::vector<int> r;
        if (compound) { for (int b = 1; b < beats; b += 2) r.push_back (b * beatLen); return r; }
        if (halfTime) { r.push_back ((beats / 2) * beatLen); return r; }
        for (int b = 1; b < beats; b += 2) r.push_back (b * beatLen);
        if (r.empty()) r.push_back (0);
        return r;
    }
};

void fillBar (Gen& g, int barStart, int genre)
{
    const bool triplet = genre == genreBlues || genre == genreJazz || g.compound;
    const int beatsInFill = g.c >= 0.55 && g.beats >= 3 ? 2 : 1;
    const int from = barStart + g.barLen - beatsInFill * g.beatLen;
    g.clearRange (from, barStart + g.barLen);
    const int step = triplet ? T8 : S16;
    const int count = (barStart + g.barLen - from) / step;
    const int seq[] = { snare, snare, tomHigh, tomHigh, tomMid, tomMid, tomLow, tomFloor, tomFloor };
    const bool heavy = genre >= genreMetal;
    for (int i = 0; i < count; ++i)
    {
        const int t = from + i * step;
        const int idx = juce::jlimit (0, 8, i * 9 / juce::jmax (1, count));
        int lane = seq[idx];
        if (g.c > 0.75 && g.chance (0.25)) lane = snare;
        g.hit (lane, t, (t - from) % g.beatLen == 0 ? 112 : 92);
        if (heavy && i % 2 == 1) g.hit (kick, t, 100);
        if ((t - from) % g.beatLen == 0) g.hit (kick, t, 100);
    }
}
} // namespace

juce::ValueTree generate (const GenOptions& o)
{
    const int genre = juce::jlimit (0, (int) numGenres - 1, o.genre);
    const bool tripletGrid = genre == genreBlues || genre == genreJazz;
    auto pattern = makePattern (juce::String (genreName (genre)) + (o.halfTime ? " (half-time)" : ""), o.tsNum, o.tsDen,
                                juce::jlimit (1, 16, o.bars), tripletGrid ? 3 : 4);
    Gen g { pattern, juce::Random ((juce::int64) o.seed * 7919 + genre * 104729 + 12345) };
    g.c = juce::jlimit (0.0, 1.0, o.complexity);
    g.barLen = barTicks (pattern);
    g.compound = o.tsDen == 8 && o.tsNum % 3 == 0 && o.tsNum > 3;
    g.beatLen = g.compound ? Q * 3 / 2 : Q * 4 / juce::jmax (1, o.tsDen);
    g.beats = juce::jmax (1, g.barLen / g.beatLen);
    const bool half = o.halfTime || genre == genreDoom;
    if (genre == genreReggae) pattern.setProperty (ids::swing, 0.25, nullptr);

    for (int bar = 0; bar < o.bars; ++bar)
    {
        const int base = bar * g.barLen;
        const bool variation = bar % 2 == 1;   // later bars vary the kick a little
        const auto bb = g.backbeats (half);
        auto backbeat = [&] (int lane, int vel) { for (int t : bb) g.hit (lane, base + t, vel); };
        auto beatKicks = [&] (std::initializer_list<int> beatsIdx) {
            for (int b : beatsIdx) if (b < g.beats) g.hit (kick, base + b * g.beatLen, 108);
        };

        switch (genre)
        {
            case genreRock:
            case genreHardRock:
            case genrePop:
            {
                const bool sixteenths = genre == genrePop ? g.c > 0.4 : g.c > 0.8;
                const int cym = genre == genreHardRock && g.c > 0.5 && g.chance (0.5) ? ride : hhClosed;
                g.pulse (base, cym, sixteenths ? S16 : E8, 96, sixteenths ? 62 : 74);
                backbeat (snare, 112);
                if (genre == genrePop && g.c > 0.6) backbeat (clap, 90);
                beatKicks ({ 0 });
                if (! half) beatKicks ({ g.beats / 2 });
                if (g.chance (0.3 + 0.5 * g.c)) g.hit (kick, base + (g.beats / 2) * g.beatLen + E8, 96);
                if (g.chance (0.25 * g.c + (variation ? 0.3 : 0.0))) g.hit (kick, base + g.beatLen + E8, 92);
                if (genre != genrePop && g.chance (0.3 * g.c)) g.hit (kick, base + 3 * S16, 84);
                if (genre == genreHardRock && g.chance (0.4 + 0.3 * g.c) && g.beats >= 4)
                {
                    g.clear (hhClosed, base + (g.beats - 1) * g.beatLen + E8);
                    g.hit (hhOpen, base + (g.beats - 1) * g.beatLen + E8, 92);
                }
                for (int t = S16; t < g.barLen; t += 2 * S16)
                    if (g.chance (0.18 * g.c)) g.hit (snare, base + t, 30);
                break;
            }
            case genrePunk:
            {
                if (g.c > 0.6)
                {   // skank / polka beat
                    for (int b = 0; b < g.beats; ++b)
                    {
                        g.hit (kick, base + b * g.beatLen, 108);
                        g.hit (snare, base + b * g.beatLen + E8, 110);
                        g.hit (g.c > 0.8 ? crash : hhClosed, base + b * g.beatLen, 96);
                    }
                }
                else
                {
                    g.pulse (base, hhClosed, E8, 104, 92);
                    backbeat (snare, 115);
                    beatKicks ({ 0, g.beats / 2 });
                    g.hit (kick, base + g.beatLen + E8, 100);
                    if (g.chance (0.4)) g.hit (kick, base + E8, 96);
                }
                break;
            }
            case genreFunk:
            {
                g.pulse (base, hhClosed, S16, 92, 58);
                backbeat (snare, 116);
                const int ghostSpots[] = { S16, 3 * S16, Q + 3 * S16, 2 * Q + S16, 3 * Q + S16, 3 * Q + 3 * S16 };
                for (int t : ghostSpots)
                    if (t < g.barLen && g.chance (0.4 + 0.4 * g.c)) g.hit (snare, base + t, 34);
                g.hit (kick, base, 110);
                const int kickSpots[] = { 3 * S16, E8 + Q, 2 * Q + E8, 2 * Q + 3 * S16, Q + S16 };
                for (int t : kickSpots)
                    if (t < g.barLen && g.chance (0.35 + 0.35 * g.c)) g.hit (kick, base + t, 96);
                if (g.beats >= 4)
                {
                    g.clear (hhClosed, base + 3 * Q + E8);
                    g.hit (hhOpen, base + 3 * Q + E8, 90);
                }
                break;
            }
            case genreBlues:
            {
                for (int b = 0; b < g.beats; ++b)
                {
                    g.hit (g.c > 0.5 ? ride : hhClosed, base + b * g.beatLen, 96);
                    g.hit (g.c > 0.5 ? ride : hhClosed, base + b * g.beatLen + 2 * T8, 70);
                }
                backbeat (snare, 108);
                beatKicks ({ 0, g.beats / 2 });
                if (g.chance (0.5 * g.c)) g.hit (kick, base + (g.beats / 2) * g.beatLen - T8, 80);
                break;
            }
            case genreReggae:
            {
                g.pulse (base, hhClosed, E8, 84, 70);
                const int three = (g.beats / 2) * g.beatLen;
                g.hit (kick, base + three, 110);
                g.hit (sideStick, base + three, 104);
                if (g.chance (0.5 * g.c)) g.hit (sideStick, base + g.beatLen + E8, 60);
                break;
            }
            case genreJazz:
            {
                for (int b = 0; b < g.beats; ++b)
                {
                    g.hit (ride, base + b * g.beatLen, 92);
                    if (b % 2 == 1) g.hit (ride, base + b * g.beatLen + 2 * T8, 70);
                    g.hit (kick, base + b * g.beatLen, 34);  // feathered
                    if (b % 2 == 1) g.hit (hhPedal, base + b * g.beatLen, 84);
                }
                for (int t = 0; t < g.barLen; t += T8)
                    if (t % g.beatLen != 0 && g.chance (0.22 * g.c + 0.05)) g.hit (snare, base + t, 44);
                break;
            }
            case genreMetal:
            {
                const int cym = g.c > 0.6 ? ride : hhClosed;
                g.pulse (base, cym, E8, 100, 84);
                backbeat (snare, 118);
                if (g.c > 0.5)
                    for (int t = 0; t < g.barLen; t += S16) g.hit (kick, base + t, 100);
                else
                    for (int t = 0; t < g.barLen; t += E8)
                        if (t % g.beatLen == 0 || g.chance (0.5)) g.hit (kick, base + t, 104);
                break;
            }
            case genreThrash:
            {
                for (int b = 0; b < g.beats; ++b)
                {
                    g.hit (kick, base + b * g.beatLen, 110);
                    g.hit (snare, base + b * g.beatLen + E8, 112);
                    g.hit (g.c > 0.4 ? crash : hhOpen, base + b * g.beatLen, 96);
                    if (g.c > 0.7) g.hit (kick, base + b * g.beatLen + E8, 100);
                }
                break;
            }
            case genreDBeat:
            {
                g.pulse (base, g.c > 0.5 ? crash : hhClosed, g.c > 0.5 ? Q : E8, 100, 86);
                // K . S K | K . S .
                const int k[] = { 0, 3 * E8, 4 * E8 };
                const int s[] = { 2 * E8, 6 * E8 };
                for (int t : k) if (t < g.barLen) g.hit (kick, base + t, 110);
                for (int t : s) if (t < g.barLen) g.hit (snare, base + t, 116);
                break;
            }
            case genreDoom:
            {
                for (int b = 0; b < g.beats; ++b) g.hit (g.c > 0.5 ? ride : crash, base + b * g.beatLen, 100);
                g.hit (kick, base, 118);
                g.hit (snare, base + (g.beats / 2) * g.beatLen, 122);
                if (g.chance (0.4 + 0.4 * g.c)) g.hit (kick, base + g.beatLen + E8, 100);
                if (g.chance (0.3 * g.c)) g.hit (kick, base + (g.beats - 1) * g.beatLen + E8, 96);
                break;
            }
            case genreDeath:
            case genreBlackMetal:
            case genreBlast:
            {
                // blast types: 0 traditional (kick/snare alternate), 1 hammer (together), 2 bomb (16th kick, 8th snare)
                int type = genre == genreBlackMetal ? (g.c > 0.5 && variation ? 1 : 0) : genre == genreBlast ? (g.c < 0.5 ? 1 : 2) : -1;
                if (genre == genreDeath && (g.c >= 0.5 || variation)) type = variation ? 2 : 0;
                const int cym = genre == genreBlackMetal ? ride : crash;
                if (type < 0)
                {   // death metal double bass groove
                    for (int t = 0; t < g.barLen; t += S16) g.hit (kick, base + t, 102);
                    backbeat (snare, 118);
                    g.pulse (base, hhClosed, E8, 100, 86);
                }
                else if (type == 0)
                {
                    for (int t = 0; t < g.barLen; t += S16)
                    {
                        const bool down = (t / S16) % 2 == 0;
                        g.hit (down ? kick : snare, base + t, down ? 104 : 108);
                        if (down) g.hit (cym, base + t, (t % g.beatLen == 0) ? 100 : 80);
                    }
                }
                else if (type == 1)
                {
                    const int step = g.c > 0.75 ? S16 : E8;
                    for (int t = 0; t < g.barLen; t += step)
                    {
                        g.hit (kick, base + t, 104);
                        g.hit (snare, base + t, 108);
                        g.hit (cym, base + t, t % g.beatLen == 0 ? 102 : 84);
                    }
                }
                else
                {
                    for (int t = 0; t < g.barLen; t += S16) g.hit (kick, base + t, 102);
                    for (int t = 0; t < g.barLen; t += E8) { g.hit (snare, base + t, 110); g.hit (cym, base + t, t % g.beatLen == 0 ? 100 : 82); }
                }
                break;
            }
            case genreGallop:
            {
                g.pulse (base, g.c > 0.5 ? crash : hhClosed, Q, 100, 90);
                backbeat (snare, 116);
                for (int b = 0; b < g.beats; ++b)
                    for (int t : { 0, E8, E8 + S16 })
                        g.hit (kick, base + b * g.beatLen + t, t == 0 ? 108 : 98);
                break;
            }
            case genreDjent:
            {
                // groups of three 16ths (K K .) running across the bar lines: a 3-over-4 polyrhythm
                g.pulse (base, china, Q, 104, 104);
                g.hit (snare, base + (g.beats / 2) * g.beatLen, 120);
                const int group = 3 * S16;
                for (int t = 0; t < g.barLen; t += S16)
                {
                    const int abs = base + t;
                    const int pos = (abs / S16) % 3;
                    if (pos == 0 || (pos == 1 && g.c > 0.4)) g.hit (kick, abs, pos == 0 ? 112 : 96);
                }
                juce::ignoreUnused (group);
                break;
            }
            default: break;
        }

        if (o.crashOnOne && bar == 0)
        {
            for (int lane : { hhClosed, hhOpen, ride }) g.clear (lane, base);
            g.hit (crash, base, 116);
            g.hit (kick, base, 112);
        }
        if (o.fill && bar == o.bars - 1)
            fillBar (g, base, genre);
    }
    return pattern;
}

// ------------------------------------------------------------------ MIDI

juce::Result exportMidi (const juce::ValueTree& d, const juce::ValueTree& pattern, double tempo, const juce::File& file)
{
    const auto c = compile (d, pattern, 1);
    if (c.events.empty()) return juce::Result::fail ("There are no drum hits to export.");
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (ppq);
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / juce::jlimit (10.0, 999.0, tempo))), 0.0);
    int pn = 0, pd = 0;
    for (auto& b : c.bars)
        if (b.num != pn || b.den != pd)
        {
            seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (b.num, b.den), b.q * ppq);
            pn = b.num; pd = b.den;
        }
    auto name = juce::MidiMessage::textMetaEvent (3, pattern.isValid() ? pattern[ids::name].toString() : juce::String ("Drums"));
    seq.addEvent (name, 0.0);
    const double swingTicksPerSec = tempo / 60.0 * ppq;
    for (auto& e : c.events)
    {
        const double t = juce::jmax (0.0, e.q * ppq + e.offsetSec * swingTicksPerSec);
        const int note = pieceInfo (e.piece).gmNote;
        const auto vel = (juce::uint8) juce::jlimit (1, 127, juce::roundToInt (e.velocity * 127.0f));
        seq.addEvent (juce::MidiMessage::noteOn (10, note, vel), t);
        seq.addEvent (juce::MidiMessage::noteOff (10, note), t + ppq / 8.0);
    }
    seq.updateMatchedPairs();
    mf.addTrack (seq);
    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! out.openedOk() || ! mf.writeTo (out, 1)) return juce::Result::fail ("Could not write " + file.getFullPathName());
    return juce::Result::ok();
}

MidiImport arrangeBars (juce::ValueTree d, std::vector<ImportBar> input, const juce::String& base, juce::UndoManager* um)
{
    MidiImport res;
    struct Bar { int start, len, num, den; };
    std::vector<Bar> bars;
    std::vector<std::vector<std::array<int, 3>>> barHits;
    // trailing empty bars after the last hit are not useful
    while (! input.empty() && input.back().hits.empty()) input.pop_back();
    if (input.empty()) { res.result = juce::Result::fail ("No playable drum notes were found."); return res; }
    int pos = 0;
    for (auto& b : input)
    {
        const int len = juce::jlimit (1, 32, b.num) * ppq * 4 / juce::jlimit (1, 32, b.den);
        bars.push_back ({ pos, len, juce::jlimit (1, 32, b.num), juce::jlimit (1, 32, b.den) });
        barHits.push_back (b.hits);
        res.hits += (int) b.hits.size();
        pos += len;
    }
    // content key per bar (velocities compared in coarse steps so tiny humanised differences still match)
    std::vector<juce::String> keys;
    for (size_t b = 0; b < bars.size(); ++b)
    {
        auto h = barHits[b];
        std::sort (h.begin(), h.end(), [] (auto& x, auto& y) { return x[1] != y[1] ? x[1] < y[1] : x[0] < y[0]; });
        juce::String k;
        k << bars[b].num << "/" << bars[b].den << ":";
        for (auto& x : h) k << x[0] << "@" << x[1] << "v" << x[2] / 16 << ",";
        keys.push_back (k);
    }
    auto phraseKey = [&] (size_t from, size_t len) { juce::String k; for (size_t i = from; i < from + len; ++i) k << keys[i] << "|"; return k; };
    auto sameMeter = [&] (size_t from, size_t len) {
        for (size_t i = from; i < from + len; ++i) if (bars[i].num != bars[from].num || bars[i].den != bars[from].den) return false;
        return true;
    };

    std::map<juce::String, juce::String> patternForPhrase;
    auto letterName = [] (int i) { juce::String s; do { s = juce::String::charToString ((juce::juce_wchar) ('A' + i % 26)) + s; i = i / 26 - 1; } while (i >= 0); return s; };
    juce::ValueTree lastSection;
    juce::String lastPattern;

    size_t i = 0;
    while (i < bars.size())
    {
        // choose the phrase length that covers the most bars by repeating; otherwise group up to 4 bars
        size_t bestLen = 0, bestReps = 1, bestCover = 0;
        for (size_t len : { (size_t) 1, (size_t) 2, (size_t) 4, (size_t) 8 })   // shortest phrase wins a tie
        {
            if (i + len > bars.size() || ! sameMeter (i, len)) continue;
            const auto k = phraseKey (i, len);
            size_t reps = 1;
            while (i + (reps + 1) * len <= bars.size() && sameMeter (i + reps * len, len) && bars[i + reps * len].num == bars[i].num
                   && phraseKey (i + reps * len, len) == k)
                ++reps;
            const bool known = patternForPhrase.count (k) > 0;
            if ((reps >= 2 || known) && len * reps > bestCover) { bestLen = len; bestReps = reps; bestCover = len * reps; }
        }
        if (bestLen == 0)
        {
            bestLen = 1;
            while (bestLen < 4 && i + bestLen < bars.size() && sameMeter (i, bestLen + 1)) ++bestLen;
            // stop before a bar that starts a repeat or a known phrase
            for (size_t l = 1; l < bestLen; ++l)
                if ((i + l + 1 < bars.size() && keys[i + l] == keys[i + l + 1]) || patternForPhrase.count (phraseKey (i + l, 1)) > 0)
                { bestLen = l; break; }
            bestReps = 1;
        }

        const auto k = phraseKey (i, bestLen);
        juce::String pid;
        if (auto it = patternForPhrase.find (k); it != patternForPhrase.end()) pid = it->second;
        else
        {
            bool triplets = false, fine = false;
            for (size_t b = i; b < i + bestLen; ++b)
                for (auto& x : barHits[b])
                {
                    if (x[1] % (ppq / 4) != 0 && x[1] % (ppq / 6) == 0) triplets = true;
                    else if (x[1] % (ppq / 4) != 0) fine = true;
                }
            auto p = makePattern (base + " " + letterName (res.patterns), bars[i].num, bars[i].den, (int) bestLen, triplets ? 6 : (fine ? 8 : 4));
            for (size_t b = i; b < i + bestLen; ++b)
                for (auto& x : barHits[b])
                    setHit (p, x[0], (int) (b - i) * bars[i].len + x[1], x[2], nullptr);
            patterns (d).appendChild (p, um);
            pid = p[ids::id].toString();
            patternForPhrase[k] = pid;
            if (res.patterns == 0) res.firstPatternId = pid;
            ++res.patterns;
        }
        if (pid == lastPattern && lastSection.isValid() && (int) lastSection[ids::repeats] + (int) bestReps <= 64)
            lastSection.setProperty (ids::repeats, (int) lastSection[ids::repeats] + (int) bestReps, um);
        else
        {
            int reps = (int) bestReps;
            while (reps > 0)
            {
                lastSection = addSection (d, pid, juce::jmin (64, reps), -1, um);
                reps -= 64;
                ++res.sections;
            }
        }
        lastPattern = pid;
        i += bestLen * bestReps;
    }
    res.bars = (int) bars.size();
    return res;
}

MidiImport importMidi (juce::ValueTree d, const juce::File& file, juce::UndoManager* um)
{
    MidiImport res;
    juce::FileInputStream in (file);
    juce::MidiFile mf;
    if (! in.openedOk() || ! mf.readFrom (in)) { res.result = juce::Result::fail ("Not a readable MIDI file."); return res; }
    if (mf.getTimeFormat() <= 0) { res.result = juce::Result::fail ("SMPTE-timed MIDI files are not supported."); return res; }
    const double scale = (double) ppq / mf.getTimeFormat();

    struct Sig { int tick, num, den; };
    struct N { int tick, note, vel; };
    std::vector<Sig> sigs;
    std::vector<std::pair<int, double>> tempos;
    std::vector<N> all, ch10;
    for (int t = 0; t < mf.getNumTracks(); ++t)
        for (auto* ev : *mf.getTrack (t))
        {
            const auto& m = ev->message;
            const int tick = juce::roundToInt (m.getTimeStamp() * scale);
            if (m.isTimeSignatureMetaEvent()) { int n = 4, dd = 4; m.getTimeSignatureInfo (n, dd); sigs.push_back ({ tick, juce::jlimit (1, 32, n), juce::jlimit (1, 32, dd) }); }
            else if (m.isTempoMetaEvent()) tempos.push_back ({ tick, 60.0 / m.getTempoSecondsPerQuarterNote() });
            else if (m.isNoteOn())
            {
                N n { tick, m.getNoteNumber(), m.getVelocity() };
                all.push_back (n);
                if (m.getChannel() == 10) ch10.push_back (n);
            }
        }
    auto& notes = ch10.empty() ? all : ch10;
    if (notes.empty()) { res.result = juce::Result::fail ("The MIDI file contains no notes."); return res; }
    std::stable_sort (sigs.begin(), sigs.end(), [] (const Sig& a, const Sig& b) { return a.tick < b.tick; });
    std::stable_sort (tempos.begin(), tempos.end());
    std::stable_sort (notes.begin(), notes.end(), [] (const N& a, const N& b) { return a.tick < b.tick; });

    // tempo
    if (! tempos.empty())
    {
        res.tempo = std::round (juce::jlimit (10.0, 999.0, tempos.front().second) * 100.0) / 100.0;  // MIDI stores whole microseconds
        bool changes = false;
        for (auto& [tick, bpm] : tempos) changes = changes || std::abs (bpm - tempos.front().second) > 0.05;
        if (changes) res.warnings.add ("The file changes tempo; the drum song plays at " + juce::String (res.tempo, 1)
                                       + " BPM throughout (tempo changes in drum songs are not supported yet).");
    }

    // bar grid following the meter changes
    struct Bar { int start, len, num, den; };
    std::vector<Bar> bars;
    const int lastTick = notes.back().tick;
    int num = 4, den = 4, pos = 0;
    size_t si = 0;
    while (pos <= lastTick && bars.size() < 4000)
    {
        while (si < sigs.size() && sigs[si].tick <= pos) { num = sigs[si].num; den = sigs[si].den; ++si; }
        const int len = num * ppq * 4 / den;
        bars.push_back ({ pos, len, num, den });
        pos += len;
    }
    if (bars.size() >= 4000) res.warnings.add ("Only the first 4000 bars were imported.");

    // hits per bar (mapped to kit pieces)
    std::map<int, int> unmapped;
    std::vector<std::vector<std::array<int, 3>>> barHits (bars.size());
    size_t bi = 0;
    for (auto& n : notes)
    {
        while (bi + 1 < bars.size() && n.tick >= bars[bi].start + bars[bi].len) ++bi;
        if (n.tick >= bars[bi].start + bars[bi].len) continue;
        const int piece = pieceForGmNote (n.note);
        if (piece < 0) { ++unmapped[n.note]; continue; }
        barHits[bi].push_back ({ piece, n.tick - bars[bi].start, juce::jlimit (1, 127, n.vel) });
        ++res.hits;
    }
    if (! unmapped.empty())
    {
        juce::StringArray list;
        int count = 0;
        for (auto& [note, c] : unmapped) { list.add (juce::String (note)); count += c; }
        res.warnings.add (juce::String (count) + " note(s) use sounds this kit doesn't have (MIDI notes " + list.joinIntoString (", ")
                          + ", e.g. tambourine or Latin percussion) and were skipped.");
    }
    // one continuous pattern holding the whole file, every hit at its exact position
    while (! bars.empty() && barHits.back().empty()) { bars.pop_back(); barHits.pop_back(); }
    if (bars.empty()) { res.result = juce::Result::fail ("No playable drum notes were found."); return res; }
    bool meterChanges = false, triplets = false, fine = false;
    for (auto& b : bars) meterChanges = meterChanges || b.num != bars.front().num || b.den != bars.front().den;
    std::vector<std::array<int, 3>> hits;
    for (size_t b = 0; b < bars.size(); ++b)
        for (auto& x : barHits[b])
        {
            const int tick = bars[b].start + x[1];
            hits.push_back ({ x[0], tick, x[2] });
            if (tick % (ppq / 4) != 0 && tick % (ppq / 6) == 0) triplets = true;
            else if (tick % (ppq / 4) != 0) fine = true;
        }
    auto p = makePattern (file.getFileNameWithoutExtension(), bars.front().num, bars.front().den, 1, triplets ? 6 : (fine ? 8 : 4));
    const int endTick = bars.back().start + bars.back().len;
    p.setProperty (ids::bars, juce::jlimit (1, maxBars, (endTick + barTicks (p) - 1) / barTicks (p)), nullptr);
    std::set<std::pair<int, int>> seen;   // the same piece twice at one tick: keep the first
    for (auto& h : hits)
    {
        if (! seen.insert ({ h[0], h[1] }).second) continue;
        juce::ValueTree hit (ids::HIT);
        hit.setProperty (ids::lane, h[0], nullptr);
        hit.setProperty (ids::tick, h[1], nullptr);
        hit.setProperty (ids::vel, h[2], nullptr);
        p.appendChild (hit, nullptr);
    }
    patterns (d).appendChild (p, um);
    if (song (d).getNumChildren() == 0) { addSection (d, p[ids::id].toString(), 1, -1, um); res.sections = 1; }
    if (meterChanges)
        res.warnings.add ("The file changes time signature; it is kept as one pattern in " + juce::String (bars.front().num) + "/"
                          + juce::String (bars.front().den) + " (every hit keeps its exact position, but bar lines after the change are drawn in that meter).");
    res.patterns = 1;
    res.bars = (int) p[ids::bars];
    res.firstPatternId = p[ids::id].toString();
    return res;
}

} // namespace ff::drums
