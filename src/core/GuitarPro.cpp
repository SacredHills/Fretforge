#include "GuitarPro.h"
#include "Timeline.h"
#include <array>
#include <cstring>
#include <map>
#include <stdexcept>

namespace ff::tab::gp
{
namespace
{
struct FormatError : std::runtime_error
{
    using std::runtime_error::runtime_error;
    explicit FormatError (const juce::String& s) : std::runtime_error (s.toStdString()) {}
};

juce::String decodeText (const char* p, size_t n)
{
    // Guitar Pro 3-5 store text in the system code page (usually Windows-1252); accept UTF-8 when it is valid.
    while (n > 0 && p[n - 1] == 0) --n;
    if (juce::CharPointer_UTF8::isValidString (p, (int) n))
        return juce::String (juce::CharPointer_UTF8 (p), n);
    juce::String s;
    s.preallocateBytes (n * 2);
    for (size_t i = 0; i < n; ++i)
        s += juce::String::charToString ((juce::juce_wchar) (unsigned char) p[i]);
    return s;
}

// ------------------------------------------------------------------ binary reader (GP3-5)

class Reader
{
public:
    Reader (const void* d, size_t n) : data (static_cast<const juce::uint8*> (d)), size (n) {}

    void need (size_t n) const
    {
        if (pos + n > size) throw FormatError ("The file ends unexpectedly (truncated or not a Guitar Pro file).");
    }
    int u8()  { need (1); return data[pos++]; }
    int s8()  { return (int) (juce::int8) u8(); }
    int i16() { need (2); const int v = (juce::int16) (data[pos] | (data[pos + 1] << 8)); pos += 2; return v; }
    int i32()
    {
        need (4);
        const auto v = (juce::int32) ((juce::uint32) data[pos] | ((juce::uint32) data[pos + 1] << 8)
                                      | ((juce::uint32) data[pos + 2] << 16) | ((juce::uint32) data[pos + 3] << 24));
        pos += 4;
        return v;
    }
    double f64() { need (8); double d; std::memcpy (&d, data + pos, 8); pos += 8; return d; }
    bool boolean() { return u8() != 0; }
    void skip (int n) { if (n < 0) throw FormatError ("Corrupt length."); need ((size_t) n); pos += (size_t) n; }

    juce::String text (int len)
    {
        if (len < 0 || len > 100000) throw FormatError ("Corrupt text field.");
        need ((size_t) len);
        auto s = decodeText (reinterpret_cast<const char*> (data + pos), (size_t) len);
        pos += (size_t) len;
        return s;
    }
    juce::String strByteLen (int fieldLen)
    {
        const int len = u8();
        need ((size_t) fieldLen);
        auto s = decodeText (reinterpret_cast<const char*> (data + pos), (size_t) juce::jmin (len, fieldLen));
        pos += (size_t) fieldLen;
        return s;
    }
    juce::String strIntUnused() { skip (4); return text (u8()); }
    juce::String strInt() { return text (i32()); }
    juce::String strIntByte() { const int len = i32() - 1; u8(); return text (juce::jmax (0, len)); }

    size_t position() const { return pos; }
    bool atEnd() const { return pos >= size; }

private:
    const juce::uint8* data;
    size_t size, pos = 0;
};

int durationFromGp (int v)
{
    switch (v)
    {
        case -2: return 1;
        case -1: return 2;
        case 0: return 4;
        case 1: return 8;
        case 2: return 16;
        case 3: return 32;
        case 4: return 64;
        default: return 4;
    }
}

struct Counters
{
    int graceNotes = 0, voice2Beats = 0, oddTuplets = 0, drumTracks = 0, otherTracks = 0;
};

class BinaryParser
{
public:
    BinaryParser (const juce::MemoryBlock& mb, Song& s) : r (mb.getData(), mb.getSize()), song (s) {}

    void parse()
    {
        readVersion();
        song.title = r.strIntUnused();
        r.strIntUnused();                         // subtitle
        song.artist = r.strIntUnused();
        song.album = r.strIntUnused();
        r.strIntUnused();                         // words
        if (version >= 500) r.strIntUnused();     // music
        r.strIntUnused();                         // copyright
        r.strIntUnused();                         // tab author
        r.strIntUnused();                         // instructions
        const int noticeLines = r.i32();
        if (noticeLines < 0 || noticeLines > 1000) throw FormatError ("Corrupt header.");
        for (int i = 0; i < noticeLines; ++i) r.strIntUnused();

        if (version < 500) r.boolean();           // triplet feel
        if (version >= 400) readLyrics();
        if (version >= 510) r.skip (19);          // RSE master effect
        if (version >= 500)
        {
            r.skip (28);                          // page size and margins
            r.i16();                              // header/footer flags
            for (int i = 0; i < 10; ++i) r.strIntByte();
            r.strIntByte();                       // tempo name
        }
        song.tempo = juce::jlimit (10.0, 999.0, (double) r.i32());
        if (version >= 510) r.boolean();          // hide tempo
        r.i32();                                  // key signature
        if (version >= 400) r.u8();               // octave

        for (int i = 0; i < 64; ++i)
        {
            Channel c;
            c.program = r.i32();
            c.volume = r.u8();
            c.balance = r.u8();
            r.skip (6);                           // chorus, reverb, phaser, tremolo, 2 blank
            channels[(size_t) i] = c;
        }
        if (version >= 500) r.skip (19 * 2 + 4);  // musical directions (coda, segno ...)

        const int barCount = r.i32(), trackCount = r.i32();
        if (barCount < 0 || barCount > 4000 || trackCount < 0 || trackCount > 128)
            throw FormatError ("Corrupt bar or track count.");

        for (int i = 0; i < barCount; ++i) readMasterBar();
        for (int i = 0; i < trackCount; ++i) readTrack();
        for (int b = 0; b < barCount; ++b)
            for (int t = 0; t < trackCount; ++t)
                readBar (song.tracks[(size_t) t]);

        if (! lyricsText.isEmpty() && juce::isPositiveAndBelow (lyricsTrack, (int) song.tracks.size()))
        {
            song.tracks[(size_t) lyricsTrack].lyrics = lyricsText;
            song.tracks[(size_t) lyricsTrack].lyricsStartBar = lyricsStart;
        }
        if (counters.graceNotes > 0)
            song.warnings.add (juce::String (counters.graceNotes) + " grace note(s) were left out (not supported yet).");
        if (counters.voice2Beats > 0)
            song.warnings.add ("Second voices (" + juce::String (counters.voice2Beats) + " beats) were left out; only voice 1 is imported.");
        if (counters.oddTuplets > 0)
            song.warnings.add (juce::String (counters.oddTuplets) + " beat(s) used tuplets other than 3, 5, 6 or 7 and were imported without the tuplet.");
    }

private:
    struct Channel { int program = 25, volume = 100, balance = 64; };
    Reader r;
    Song& song;
    int version = 0;
    std::array<Channel, 64> channels {};
    juce::String lyricsText;
    int lyricsTrack = -1, lyricsStart = 0;
    Counters counters;

    void readVersion()
    {
        const auto v = r.strByteLen (30);
        if (! v.startsWith ("FICHIER GUITAR PRO v"))
            throw FormatError (v.startsWith ("FICHIER GUITARE PRO") ? "Guitar Pro 1 and 2 files are not supported."
                                                                     : "Not a Guitar Pro 3-5 file.");
        const auto num = v.fromFirstOccurrenceOf ("v", false, false);
        version = num.upToFirstOccurrenceOf (".", false, false).getIntValue() * 100
                  + num.fromFirstOccurrenceOf (".", false, false).getIntValue();
        if (version < 300 || version >= 600)
            throw FormatError ("Unsupported Guitar Pro version " + num + ".");
        song.format = "Guitar Pro " + num;
    }

    void readLyrics()
    {
        lyricsTrack = r.i32() - 1;
        for (int i = 0; i < 5; ++i)
        {
            const int start = r.i32() - 1;
            const auto text = r.strInt();
            if (i == 0) { lyricsText = text; lyricsStart = juce::jmax (0, start); }
        }
    }

    void readMasterBar()
    {
        MasterBar mb;
        const MasterBar* prev = song.masterBars.empty() ? nullptr : &song.masterBars.back();
        if (prev != nullptr) { mb.num = prev->num; mb.den = prev->den; mb.key = prev->key; }
        const int flags = r.u8();
        if (flags & 0x01) mb.num = r.u8();
        if (flags & 0x02) mb.den = r.u8();
        mb.repeatStart = (flags & 0x04) != 0;
        if (flags & 0x08) mb.repeatCount = r.u8() + (version >= 500 ? 0 : 1);
        if ((flags & 0x10) && version < 500)
        {
            // GP3/4 store the number of the ending; turn it into a pass mask not used by earlier endings
            int used = 0;
            for (int i = (int) song.masterBars.size() - 1; i >= 0; --i)
            {
                const auto& m = song.masterBars[(size_t) i];
                if ((m.repeatCount > 0 && i != (int) song.masterBars.size() - 1) || m.repeatStart) break;
                used |= m.alternateEndings;
            }
            const int n = r.u8();
            for (int i = 0; i < 8; ++i)
                if (n > i && (used & (1 << i)) == 0) mb.alternateEndings |= 1 << i;
        }
        if (flags & 0x20)
        {
            mb.marker = r.strIntByte();
            r.skip (4);                           // colour
        }
        if (flags & 0x40)
        {
            mb.key = juce::jlimit (-7, 7, r.s8());
            mb.keyChange = true;
            r.u8();                               // major / minor
        }
        if (version >= 500)
        {
            if (flags & 0x03) r.skip (4);         // beaming
            mb.alternateEndings = r.u8();
            r.u8();                               // triplet feel
            r.u8();
        }
        mb.num = juce::jlimit (1, 32, mb.num);
        mb.den = juce::jlimit (1, 64, mb.den);
        if (song.masterBars.empty()) mb.keyChange = true;
        song.masterBars.push_back (mb);
    }

    void readTrack()
    {
        Track t;
        const int flags = r.u8();
        t.name = r.strByteLen (40).trim();
        t.drums = (flags & 0x01) != 0;
        t.solo = (flags & 0x10) != 0;
        t.mute = (flags & 0x20) != 0;
        const int strings = r.i32();
        std::vector<int> high;                    // highest string first, as stored
        for (int i = 0; i < 7; ++i)
        {
            const int v = r.i32();
            if (i < strings) high.push_back (juce::jlimit (0, 127, v));
        }
        t.tuning.assign (high.rbegin(), high.rend());
        const int port = r.i32() - 1, channel = r.i32() - 1;
        r.i32();                                  // effect channel
        r.i32();                                  // fret count
        t.capo = juce::jlimit (0, 24, r.i32());
        r.skip (4);                               // colour
        const int ci = port * 16 + channel;
        if (juce::isPositiveAndBelow (ci, 64))
        {
            const auto& c = channels[(size_t) ci];
            t.program = c.program;
            t.volume = juce::jlimit (0.0f, 1.0f, (float) c.volume / 127.0f);
            t.pan = juce::jlimit (-1.0f, 1.0f, ((float) c.balance - 64.0f) / 64.0f);
        }
        if (version >= 500)
        {
            r.u8();                               // staff flags
            r.u8(); r.u8(); r.u8(); r.u8();       // auto accentuation, bank ...
            r.i32(); r.i32(); r.i32();            // clef mode, ...
            r.skip (10);
            r.u8(); r.u8();
            r.skip (16);                          // RSE bank
            if (version >= 510) { r.skip (4); r.strIntByte(); r.strIntByte(); }
        }
        t.bass = (t.program >= 32 && t.program <= 39)
                 || (! t.tuning.empty() && t.tuning.size() <= 6 && t.tuning.front() <= 33);
        if (t.tuning.empty()) t.tuning = { 40, 45, 50, 55, 59, 64 };
        song.tracks.push_back (t);
    }

    void readBar (Track& t)
    {
        Bar bar;
        int voices = 1;
        if (version >= 500) { r.u8(); voices = 2; }
        for (int v = 0; v < voices; ++v)
        {
            const int n = r.i32();
            if (n < 0 || n > 256) throw FormatError ("Corrupt beat count.");
            for (int i = 0; i < n; ++i)
            {
                Beat b = readBeat (t);
                if (v == 0) bar.beats.push_back (std::move (b));
                else if (! b.notes.empty()) ++counters.voice2Beats;
            }
        }
        t.bars.push_back (std::move (bar));
    }

    Beat readBeat (const Track& t)
    {
        Beat b;
        const int flags = r.u8();
        if (flags & 0x01) b.dots = 1;
        if (flags & 0x40) r.u8();                 // empty / rest status
        b.dur = durationFromGp (r.s8());
        if (flags & 0x20)
        {
            const int tup = r.i32();
            if (tup == 3 || tup == 5 || tup == 6 || tup == 7) b.tuplet = tup;
            else if (tup > 1) ++counters.oddTuplets;
        }
        if (flags & 0x02) readChord (b, (int) t.tuning.size());
        if (flags & 0x04) b.text = r.strIntUnused();
        int beatHarmonic = harmNone;
        bool beatVibrato = false, beatTap = false;
        if (flags & 0x08) readBeatEffects (b, beatHarmonic, beatVibrato, beatTap);
        if (flags & 0x10) readMixTable (b);

        const int stringFlags = r.u8();
        const int ns = (int) t.tuning.size();
        tremoloPicked = false;
        for (int i = 6; i >= 0; --i)
        {
            const int stringIndex = 6 - i;        // 0 = highest string
            if ((stringFlags & (1 << i)) == 0) continue;
            Note n;
            const bool keep = readNote (n);
            n.string = ns - 1 - stringIndex;
            if (! keep || stringIndex >= ns || t.drums) continue;
            if (beatHarmonic != harmNone) n.harmonic = beatHarmonic;
            if (beatVibrato) n.vibrato = true;
            if (beatTap) n.tap = true;
            b.notes.push_back (n);
        }
        if (tremoloPicked) b.tremolo = true;
        if (version >= 500)
        {
            const int flags2 = r.i16();
            if (flags2 & 0x800) r.u8();           // break secondary beams
        }
        return b;
    }

    void readChord (Beat& b, int stringCount)
    {
        auto readFrets = [&] (int count) {
            juce::String f;
            std::vector<int> frets;
            for (int i = 0; i < count; ++i) frets.push_back (r.i32());
            // stored highest string first; FretForge writes lowest first
            for (int i = juce::jmin (count, stringCount) - 1; i >= 0; --i)
                f << (frets[(size_t) i] < 0 ? juce::String ("x") : juce::String (frets[(size_t) i])) << (i > 0 ? " " : "");
            b.chordFrets = f;
        };
        if (version >= 500)
        {
            r.skip (17);
            b.chord = r.strByteLen (21);
            r.skip (4);
            r.i32();                              // first fret
            readFrets (7);
            r.skip (1 + 5 + 26);
            return;
        }
        if (r.u8() != 0)
        {
            if (version >= 400)
            {
                r.skip (16);
                b.chord = r.strByteLen (21);
                r.skip (4);
                r.i32();
                readFrets (7);
                r.skip (1 + 5 + 26);
            }
            else
            {
                r.skip (25);
                b.chord = r.strByteLen (34);
                r.i32();
                readFrets (6);
                r.skip (36);
            }
        }
        else
        {
            const int strings = version >= 406 ? 7 : 6;
            b.chord = r.strIntByte();
            if (r.i32() > 0) readFrets (strings);
        }
        b.chord = b.chord.trim();
    }

    void readBeatEffects (Beat& b, int& harmonic, bool& vibrato, bool& tap)
    {
        const int flags = r.u8();
        const int flags2 = version >= 400 ? r.u8() : 0;
        if ((version < 400 && (flags & 0x01)) || (flags & 0x02)) vibrato = true;
        if (flags & 0x20)
        {
            const int type = r.s8();
            if (version >= 400) { if (type == 1) tap = true; }
            else
            {
                const int value = r.i32();
                if (type == 0) b.whammy = (float) value / 50.0f;   // GP3 tremolo bar dive
                else if (type == 1) tap = true;
            }
        }
        if (flags2 & 0x04) b.whammy = readBendPoints (true);
        if (flags & 0x40)
        {
            int up, down;
            if (version < 500) { down = r.u8(); up = r.u8(); }
            else { up = r.u8(); down = r.u8(); }
            if (up > 0) b.stroke = strokeUp;
            else if (down > 0) b.stroke = strokeDown;
        }
        if (flags2 & 0x02)
        {
            const int pick = r.s8();
            if (b.stroke == strokeNone) b.stroke = pick == 1 ? strokeUp : pick == 2 ? strokeDown : strokeNone;
        }
        if (version < 400)
        {
            if (flags & 0x04) harmonic = harmNatural;
            if (flags & 0x08) harmonic = harmArtificial;
        }
    }

    /** Returns the extreme value of a bend / whammy curve in semitones. */
    float readBendPoints (bool whammy, bool* pre = nullptr)
    {
        r.u8();                                   // type
        r.i32();                                  // value
        const int count = r.i32();
        if (count < 0 || count > 200) throw FormatError ("Corrupt bend.");
        float best = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            r.i32();                              // position 0..60
            const float v = (float) r.i32() / 50.0f;   // 100 = full step = 2 semitones
            r.u8();                               // vibrato
            if (i == 0 && pre != nullptr) *pre = v > 0.0f;
            if (whammy ? std::abs (v) > std::abs (best) : v > best) best = v;
        }
        return best;
    }

    void readMixTable (Beat& b)
    {
        r.s8();                                   // instrument
        if (version >= 500) r.skip (16);          // RSE
        const int volume = r.s8(), balance = r.s8(), chorus = r.s8(), reverb = r.s8(), phaser = r.s8(), tremolo = r.s8();
        if (version >= 500) r.strIntByte();       // tempo name
        const int tempo = r.i32();
        for (int v : { volume, balance, chorus, reverb, phaser, tremolo })
            if (v >= 0) r.u8();                   // transition durations
        if (tempo >= 0)
        {
            r.u8();
            if (version >= 510) r.u8();
            if (tempo >= 10) b.tempo = juce::jlimit (10.0, 999.0, (double) tempo);
        }
        if (version >= 400) r.u8();               // apply-to-all flags
        if (version >= 500) r.s8();               // wah
        if (version >= 510) { r.strIntByte(); r.strIntByte(); }
    }

    bool readNote (Note& n)
    {
        const int flags = r.u8();
        n.accent = (flags & 0x02) || (flags & 0x40);
        n.ghost = (flags & 0x04) != 0;
        if (flags & 0x20)
        {
            const int type = r.u8();
            n.tie = type == 2;
            n.dead = type == 3;
        }
        if ((flags & 0x01) && version < 500) r.skip (2);
        if (flags & 0x10) r.s8();                 // dynamics
        const bool hasFret = (flags & 0x20) != 0;
        if (hasFret)
        {
            const int f = r.s8();
            if (f < 0) n.dead = true;
            n.fret = juce::jlimit (0, maxFret, f);
        }
        if (flags & 0x80) r.skip (2);             // fingering
        if (version >= 500)
        {
            if (flags & 0x01) r.f64();            // duration percent
            r.u8();
        }
        if (flags & 0x08) readNoteEffects (n);
        return hasFret;
    }

    void readNoteEffects (Note& n)
    {
        const int flags = r.u8();
        const int flags2 = version >= 400 ? r.u8() : 0;
        if (flags & 0x01)
        {
            bool pre = false;
            n.bend = juce::jmax (0.0f, readBendPoints (false, &pre));
            n.prebend = pre;
        }
        if (flags & 0x10)
        {
            r.s8(); r.s8(); r.s8(); r.u8();       // grace: fret, dynamics, transition, duration
            if (version >= 500) r.u8();
            ++counters.graceNotes;
        }
        if (flags2 & 0x04) { r.u8(); tremoloPicked = true; }
        if (flags2 & 0x08)
        {
            const int type = r.s8();
            if (version >= 500)
            {
                if (type & 0x03) n.slide = slideTo;
                else if (type & 0x0c) n.slide = slideOutDown;
                else if (type & 0x30) n.slide = slideInBelow;
            }
            else
            {
                if (type == 1 || type == 2) n.slide = slideTo;
                else if (type == 3 || type == 4) n.slide = slideOutDown;
                else if (type < 0) n.slide = slideInBelow;
            }
        }
        else if (version < 400 && (flags & 0x04))
            n.slide = slideTo;
        if (flags2 & 0x10)
        {
            const int type = r.u8();
            if (version >= 500)
            {
                if (type == 2) r.skip (3);
                else if (type == 3) r.skip (1);
            }
            n.harmonic = type == 1 ? harmNatural : harmArtificial;
        }
        if (flags2 & 0x20) r.skip (2);            // trill
        n.letRing = (flags & 0x08) != 0;
        n.hammer = (flags & 0x02) != 0;
        if (flags2 & 0x40) n.vibrato = true;
        n.palmMute = (flags2 & 0x02) != 0;
        n.staccato = (flags2 & 0x01) != 0;
    }

public:
    bool tremoloPicked = false; // set by the last note read; consumed per beat
};

// ------------------------------------------------------------------ GPIF (GP6 / GP7)

juce::String childText (const juce::XmlElement* e, const juce::String& name)
{
    if (e == nullptr) return {};
    if (auto* c = e->getChildByName (name)) return c->getAllSubText().trim();
    return {};
}

const juce::XmlElement* findProperty (const juce::XmlElement* props, const juce::String& name)
{
    if (props == nullptr) return nullptr;
    for (auto* p : props->getChildWithTagNameIterator ("Property"))
        if (p->getStringAttribute ("name") == name) return p;
    return nullptr;
}

int noteValueToDur (const juce::String& v)
{
    if (v == "Long" || v == "DoubleWhole" || v == "Whole") return 1;
    if (v == "Half") return 2;
    if (v == "Quarter") return 4;
    if (v == "Eighth") return 8;
    if (v == "16th") return 16;
    if (v == "32nd") return 32;
    if (v == "64th" || v == "128th" || v == "256th") return 64;
    return 4;
}

double tempoFromAutomation (const juce::String& value)
{
    const auto parts = juce::StringArray::fromTokens (value, " ", "");
    double bpm = parts.isEmpty() ? 120.0 : parts[0].getDoubleValue();
    const int ref = parts.size() > 1 ? parts[1].getIntValue() : 2;
    switch (ref)
    {
        case 1: bpm *= 0.5; break;   // eighth
        case 3: bpm *= 1.5; break;   // dotted quarter
        case 4: bpm *= 2.0; break;   // half
        case 5: bpm *= 3.0; break;   // dotted half
        default: break;              // quarter
    }
    return juce::jlimit (10.0, 999.0, bpm);
}

class GpifParser
{
public:
    explicit GpifParser (Song& s) : song (s) {}

    void parse (const juce::String& xmlText)
    {
        auto doc = juce::XmlDocument::parse (xmlText);
        if (doc == nullptr || ! doc->hasTagName ("GPIF"))
            throw FormatError ("The Guitar Pro score data is not valid GPIF XML.");
        const int gpVersion = childText (doc.get(), "GPVersion").getIntValue();
        if (song.format.isEmpty()) song.format = gpVersion >= 7 ? "Guitar Pro 7+" : "Guitar Pro 6";

        if (auto* score = doc->getChildByName ("Score"))
        {
            song.title = childText (score, "Title");
            song.artist = childText (score, "Artist");
            song.album = childText (score, "Album");
        }

        index (doc->getChildByName ("Rhythms"), "Rhythm", rhythms);
        index (doc->getChildByName ("Notes"), "Note", notes);
        index (doc->getChildByName ("Beats"), "Beat", beats);
        index (doc->getChildByName ("Voices"), "Voice", voices);
        index (doc->getChildByName ("Bars"), "Bar", bars);

        std::vector<const juce::XmlElement*> trackElems;
        if (auto* ts = doc->getChildByName ("Tracks"))
            for (auto* t : ts->getChildWithTagNameIterator ("Track"))
                trackElems.push_back (t);
        for (auto* t : trackElems) song.tracks.push_back (readTrack (t));

        if (auto* mbs = doc->getChildByName ("MasterBars"))
            for (auto* mb : mbs->getChildWithTagNameIterator ("MasterBar"))
                readMasterBar (mb);

        // tempo automations
        if (auto* mt = doc->getChildByName ("MasterTrack"))
            if (auto* autos = mt->getChildByName ("Automations"))
                for (auto* a : autos->getChildWithTagNameIterator ("Automation"))
                {
                    if (childText (a, "Type") != "Tempo") continue;
                    const int bar = childText (a, "Bar").getIntValue();
                    const double bpm = tempoFromAutomation (childText (a, "Value"));
                    if (bar == 0) song.tempo = bpm;
                    else if (juce::isPositiveAndBelow (bar, (int) song.masterBars.size()))
                        song.masterBars[(size_t) bar].tempo = bpm;
                }

        if (counters.graceNotes > 0)
            song.warnings.add (juce::String (counters.graceNotes) + " grace note(s) were left out (not supported yet).");
        if (counters.voice2Beats > 0)
            song.warnings.add ("Additional voices (" + juce::String (counters.voice2Beats) + " beats) were left out; only voice 1 is imported.");
        if (counters.oddTuplets > 0)
            song.warnings.add (juce::String (counters.oddTuplets) + " beat(s) used tuplets other than 3, 5, 6 or 7 and were imported without the tuplet.");
    }

private:
    Song& song;
    std::map<juce::String, const juce::XmlElement*> rhythms, notes, beats, voices, bars;
    std::vector<std::map<juce::String, std::pair<juce::String, juce::String>>> chordsPerTrack; // id -> (name, frets)
    Counters counters;

    static void index (const juce::XmlElement* parent, const juce::String& tag, std::map<juce::String, const juce::XmlElement*>& out)
    {
        if (parent == nullptr) return;
        for (auto* e : parent->getChildWithTagNameIterator (tag))
            out[e->getStringAttribute ("id")] = e;
    }

    Track readTrack (const juce::XmlElement* t)
    {
        Track tr;
        tr.name = childText (t, "Name");
        const juce::XmlElement* props = t->getChildByName ("Properties");
        // GP7: tuning and capo live on the first staff
        if (auto* staves = t->getChildByName ("Staves"))
            if (auto* staff = staves->getChildByName ("Staff"))
                if (auto* sp = staff->getChildByName ("Properties"))
                    if (findProperty (sp, "Tuning") != nullptr) props = sp;

        std::map<juce::String, std::pair<juce::String, juce::String>> chords;
        if (auto* tuning = findProperty (props, "Tuning"))
            for (auto& tok : juce::StringArray::fromTokens (childText (tuning, "Pitches"), " ", ""))
                tr.tuning.push_back (juce::jlimit (0, 127, tok.getIntValue()));
        if (auto* capo = findProperty (props, "CapoFret"))
            tr.capo = juce::jlimit (0, 24, childText (capo, "Fret").getIntValue());
        for (auto* collName : { "DiagramCollection", "ChordCollection" })
            if (auto* coll = findProperty (props, collName))
                if (auto* items = coll->getChildByName ("Items"))
                    for (auto* item : items->getChildWithTagNameIterator ("Item"))
                    {
                        juce::String frets;
                        if (auto* d = item->getChildByName ("Diagram"))
                        {
                            const int sc = d->getIntAttribute ("stringCount", (int) tr.tuning.size());
                            const int base = d->getIntAttribute ("baseFret", 0);
                            std::vector<int> f ((size_t) juce::jlimit (1, 10, sc), -1);
                            for (auto* fe : d->getChildWithTagNameIterator ("Fret"))
                            {
                                const int s = fe->getIntAttribute ("string", -1);
                                if (juce::isPositiveAndBelow (s, (int) f.size())) f[(size_t) s] = fe->getIntAttribute ("fret") + base;
                            }
                            for (size_t i = 0; i < f.size(); ++i)
                                frets << (f[i] < 0 ? juce::String ("x") : juce::String (f[i])) << (i + 1 < f.size() ? " " : "");
                        }
                        chords[item->getStringAttribute ("id")] = { item->getStringAttribute ("name").trim(), frets };
                    }
        chordsPerTrack.push_back (chords);

        const auto instrType = (childText (t->getChildByName ("InstrumentSet"), "Type")
                                + " " + (t->getChildByName ("Instrument") != nullptr ? t->getChildByName ("Instrument")->getStringAttribute ("ref") : juce::String()))
                                   .toLowerCase();
        tr.drums = instrType.contains ("drum") || instrType.contains ("percussion");
        tr.bass = instrType.contains ("bass") || (! tr.tuning.empty() && tr.tuning.size() <= 6 && tr.tuning.front() <= 33);
        const auto state = childText (t, "PlaybackState");
        tr.solo = state == "Solo";
        tr.mute = state == "Mute";
        if (auto* lyr = t->getChildByName ("Lyrics"))
            if (auto* line = lyr->getChildByName ("Line"))
            {
                tr.lyrics = childText (line, "Text");
                tr.lyricsStartBar = juce::jmax (0, childText (line, "Offset").getIntValue());
            }
        return tr;
    }

    void readMasterBar (const juce::XmlElement* e)
    {
        MasterBar mb;
        if (! song.masterBars.empty())
        {
            mb.num = song.masterBars.back().num;
            mb.den = song.masterBars.back().den;
            mb.key = song.masterBars.back().key;
        }
        const auto time = childText (e, "Time");
        if (time.containsChar ('/'))
        {
            mb.num = juce::jlimit (1, 32, time.upToFirstOccurrenceOf ("/", false, false).getIntValue());
            mb.den = juce::jlimit (1, 64, time.fromFirstOccurrenceOf ("/", false, false).getIntValue());
        }
        if (auto* key = e->getChildByName ("Key"))
        {
            const int k = juce::jlimit (-7, 7, childText (key, "AccidentalCount").getIntValue());
            mb.keyChange = song.masterBars.empty() || k != mb.key;
            mb.key = k;
        }
        if (auto* rep = e->getChildByName ("Repeat"))
        {
            mb.repeatStart = rep->getStringAttribute ("start").equalsIgnoreCase ("true");
            if (rep->getStringAttribute ("end").equalsIgnoreCase ("true"))
                mb.repeatCount = juce::jmax (2, rep->getIntAttribute ("count", 2));
        }
        for (auto& tok : juce::StringArray::fromTokens (childText (e, "AlternateEndings"), " ", ""))
            if (tok.getIntValue() >= 1 && tok.getIntValue() <= 16) mb.alternateEndings |= 1 << (tok.getIntValue() - 1);
        if (auto* sec = e->getChildByName ("Section"))
        {
            mb.marker = childText (sec, "Text");
            if (mb.marker.isEmpty()) mb.marker = childText (sec, "Letter");
        }
        song.masterBars.push_back (mb);

        const auto barIds = juce::StringArray::fromTokens (childText (e, "Bars"), " ", "");
        for (int t = 0; t < (int) song.tracks.size(); ++t)
            song.tracks[(size_t) t].bars.push_back (readBar (barIds[t], t));
    }

    Bar readBar (const juce::String& id, int trackIndex)
    {
        Bar bar;
        auto it = bars.find (id);
        if (it == bars.end()) return bar;
        const auto voiceIds = juce::StringArray::fromTokens (childText (it->second, "Voices"), " ", "");
        bool first = true;
        for (auto& vid : voiceIds)
        {
            if (vid == "-1") continue;
            auto v = voices.find (vid);
            if (v == voices.end()) continue;
            for (auto& bid : juce::StringArray::fromTokens (childText (v->second, "Beats"), " ", ""))
            {
                auto b = beats.find (bid);
                if (b == beats.end()) continue;
                if (b->second->getChildByName ("GraceNotes") != nullptr)
                {
                    counters.graceNotes += juce::StringArray::fromTokens (childText (b->second, "Notes"), " ", "").size();
                    continue;
                }
                Beat beat = readBeat (b->second, trackIndex);
                if (first) bar.beats.push_back (std::move (beat));
                else if (! beat.notes.empty()) ++counters.voice2Beats;
            }
            first = false;
        }
        return bar;
    }

    Beat readBeat (const juce::XmlElement* e, int trackIndex)
    {
        Beat b;
        if (auto* rh = e->getChildByName ("Rhythm"))
        {
            auto r = rhythms.find (rh->getStringAttribute ("ref"));
            if (r != rhythms.end())
            {
                b.dur = noteValueToDur (childText (r->second, "NoteValue"));
                if (auto* dot = r->second->getChildByName ("AugmentationDot")) b.dots = juce::jlimit (0, 2, dot->getIntAttribute ("count"));
                if (auto* tup = r->second->getChildByName ("PrimaryTuplet"))
                {
                    const int num = tup->getIntAttribute ("num"), den = tup->getIntAttribute ("den");
                    if ((num == 3 && den == 2) || (num == 5 && den == 4) || (num == 6 && den == 4) || (num == 7 && den == 4)) b.tuplet = num;
                    else if (num > 1 && num != den) ++counters.oddTuplets;
                }
            }
        }
        if (auto* trem = e->getChildByName ("Tremolo")) b.tremolo = trem->getAllSubText().isNotEmpty();
        b.text = childText (e, "FreeText");
        if (auto* chord = e->getChildByName ("Chord"))
        {
            const auto& chords = chordsPerTrack[(size_t) trackIndex];
            auto c = chords.find (chord->getAllSubText().trim());
            if (c != chords.end()) { b.chord = c->second.first; b.chordFrets = c->second.second; }
        }
        if (auto* lyr = e->getChildByName ("Lyrics"))
            if (auto* line = lyr->getChildByName ("Line"))
                b.lyric = line->getAllSubText().trim();
        if (auto* props = e->getChildByName ("Properties"))
        {
            if (auto* p = findProperty (props, "Brush"))
                b.stroke = childText (p, "Direction") == "Up" ? strokeUp : strokeDown;
            else if (auto* pk = findProperty (props, "PickStroke"))
                b.stroke = childText (pk, "Direction") == "Up" ? strokeUp : strokeDown;
            if (findProperty (props, "WhammyBar") != nullptr)
            {
                float best = 0.0f;
                for (auto* n : { "WhammyBarOriginValue", "WhammyBarMiddleValue", "WhammyBarDestinationValue" })
                    if (auto* p = findProperty (props, n))
                    {
                        const float v = (float) childText (p, "Float").getDoubleValue() / 50.0f;
                        if (std::abs (v) > std::abs (best)) best = v;
                    }
                b.whammy = best;
            }
        }
        if (auto* wh = e->getChildByName ("Whammy"))
        {
            float best = 0.0f;
            for (auto* a : { "originValue", "middleValue", "destinationValue" })
            {
                const float v = (float) wh->getDoubleAttribute (a) / 50.0f;
                if (std::abs (v) > std::abs (best)) best = v;
            }
            b.whammy = best;
        }
        const auto& tr = song.tracks[(size_t) trackIndex];
        for (auto& nid : juce::StringArray::fromTokens (childText (e, "Notes"), " ", ""))
        {
            auto ne = notes.find (nid);
            if (ne == notes.end() || tr.drums) continue;
            Note n;
            if (readNote (ne->second, n, (int) tr.tuning.size()))
                b.notes.push_back (n);
        }
        return b;
    }

    bool readNote (const juce::XmlElement* e, Note& n, int stringCount)
    {
        bool hasString = false, hasFret = false;
        if (auto* tie = e->getChildByName ("Tie"))
            n.tie = tie->getStringAttribute ("destination").equalsIgnoreCase ("true");
        if (e->getChildByName ("LetRing") != nullptr) n.letRing = true;
        if (e->getChildByName ("Vibrato") != nullptr) n.vibrato = true;
        if (e->getChildByName ("AntiAccent") != nullptr) n.ghost = true;
        if (auto* acc = e->getChildByName ("Accent"))
        {
            const int f = acc->getAllSubText().getIntValue();
            n.staccato = (f & 0x01) != 0;
            n.accent = (f & 0x0c) != 0;
        }
        if (auto* props = e->getChildByName ("Properties"))
            for (auto* p : props->getChildWithTagNameIterator ("Property"))
            {
                const auto name = p->getStringAttribute ("name");
                if (name == "String") { n.string = childText (p, "String").getIntValue(); hasString = true; }
                else if (name == "Fret") { n.fret = juce::jlimit (0, maxFret, childText (p, "Fret").getIntValue()); hasFret = true; }
                else if (name == "Slide")
                {
                    const int f = childText (p, "Flags").getIntValue();
                    if (f & 0x03) n.slide = slideTo;
                    else if (f & 0x0c) n.slide = slideOutDown;
                    else if (f & 0x30) n.slide = slideInBelow;
                }
                else if (name == "HopoOrigin") n.hammer = true;
                else if (name == "Tapped" || name == "LeftHandTapped") n.tap = true;
                else if (name == "Muted") n.dead = true;
                else if (name == "PalmMuted") n.palmMute = true;
                else if (name == "HarmonicType")
                {
                    const auto t = childText (p, "HType").toLowerCase();
                    n.harmonic = t == "natural" ? harmNatural : (t.isEmpty() || t == "noharmonic") ? harmNone : harmArtificial;
                }
                else if (name == "BendOriginValue")
                    n.prebend = childText (p, "Float").getDoubleValue() > 0.0;
                else if (name == "BendMiddleValue" || name == "BendDestinationValue" || name == "BendOriginValue")
                    n.bend = juce::jmax (n.bend, (float) childText (p, "Float").getDoubleValue() / 50.0f);
            }
        if (n.prebend)
            if (auto* props = e->getChildByName ("Properties"))
                if (auto* p = findProperty (props, "BendOriginValue"))
                    n.bend = juce::jmax (n.bend, (float) childText (p, "Float").getDoubleValue() / 50.0f);
        return hasString && hasFret && juce::isPositiveAndBelow (n.string, stringCount);
    }
};

// ------------------------------------------------------------------ GPX container

class BitReader
{
public:
    BitReader (const juce::uint8* d, size_t n) : data (d), size (n) {}
    int bit()
    {
        if (bitPos >= 8)
        {
            if (pos >= size) throw FormatError ("Truncated GPX data.");
            current = data[pos++];
            bitPos = 0;
        }
        return (current >> (7 - bitPos++)) & 1;
    }
    int bits (int n) { int v = 0; for (int i = n - 1; i >= 0; --i) v |= bit() << i; return v; }
    int bitsReversed (int n) { int v = 0; for (int i = 0; i < n; ++i) v |= bit() << i; return v; }
    int byte() { return bits (8); }
    bool exhausted() const { return pos >= size && bitPos >= 8; }
private:
    const juce::uint8* data;
    size_t size, pos = 0;
    int bitPos = 8, current = 0;
};

int le32 (const juce::uint8* p) { return (int) ((juce::uint32) p[0] | ((juce::uint32) p[1] << 8) | ((juce::uint32) p[2] << 16) | ((juce::uint32) p[3] << 24)); }

juce::MemoryBlock decompressBcfz (const juce::uint8* d, size_t n)
{
    BitReader br (d, n);
    juce::MemoryBlock out;
    std::vector<juce::uint8> buf;
    juce::uint8 lenBytes[4];
    for (auto& b : lenBytes) b = (juce::uint8) br.byte();
    const int expected = le32 (lenBytes);
    if (expected < 0 || expected > 256 * 1024 * 1024) throw FormatError ("Corrupt GPX container.");
    buf.reserve ((size_t) expected);
    try
    {
        while ((int) buf.size() < expected)
        {
            if (br.bits (1) == 1)
            {
                const int wordSize = br.bits (4);
                const int offset = br.bitsReversed (wordSize);
                const int len = br.bitsReversed (wordSize);
                const int src = (int) buf.size() - offset;
                if (offset <= 0 || src < 0) throw FormatError ("Corrupt GPX data.");
                const int count = juce::jmin (offset, len);
                for (int i = 0; i < count; ++i) buf.push_back (buf[(size_t) (src + i)]);
            }
            else
            {
                const int len = br.bitsReversed (2);
                for (int i = 0; i < len; ++i) buf.push_back ((juce::uint8) br.byte());
            }
        }
    }
    catch (const FormatError&)
    {
        if (buf.empty()) throw;  // data may end mid-stream; keep what was decoded
    }
    out.append (buf.data(), buf.size());
    return out;
}
} // namespace

// ------------------------------------------------------------------ public API

juce::MemoryBlock extractGpxFile (const juce::MemoryBlock& container, const juce::String& wanted)
{
    try
    {
        const auto* d = static_cast<const juce::uint8*> (container.getData());
        const size_t n = container.getSize();
        if (n < 8) return {};
        juce::MemoryBlock fs;
        if (std::memcmp (d, "BCFZ", 4) == 0)
        {
            auto raw = decompressBcfz (d + 4, n - 4);
            if (raw.getSize() < 4) return {};
            fs.append (static_cast<const char*> (raw.getData()) + 4, raw.getSize() - 4); // skip inner "BCFS"
        }
        else if (std::memcmp (d, "BCFS", 4) == 0)
            fs.append (d + 4, n - 4);
        else
            return {};

        const auto* data = static_cast<const juce::uint8*> (fs.getData());
        const size_t size = fs.getSize();
        constexpr size_t sector = 0x1000;
        for (size_t offset = sector; offset + 0x94 + 4 <= size; offset += sector)
        {
            if (le32 (data + offset) != 2) continue;
            juce::String name;
            for (size_t i = 0; i < 127 && data[offset + 4 + i] != 0; ++i) name += (char) data[offset + 4 + i];
            const int fileSize = le32 (data + offset + 0x8c);
            if (fileSize < 0 || (size_t) fileSize > size) continue;
            if (! name.equalsIgnoreCase (wanted)) continue;
            juce::MemoryBlock file;
            for (size_t k = 0;; ++k)
            {
                const size_t ptr = offset + 0x94 + 4 * k;
                if (ptr + 4 > size) break;
                const int s = le32 (data + ptr);
                if (s <= 0) break;
                const size_t so = (size_t) s * sector;
                if (so >= size) break;
                file.append (data + so, juce::jmin (sector, size - so));
                if ((int) file.getSize() >= fileSize) break;
            }
            file.setSize ((size_t) juce::jmin ((int) file.getSize(), fileSize));
            return file;
        }
    }
    catch (const FormatError&) {}
    return {};
}

namespace
{
/** Tied notes take the fret of the note they continue (GP3-5 do not always store it). */
void resolveTies (Song& song)
{
    for (auto& tr : song.tracks)
    {
        std::vector<int> last (tr.tuning.size(), -1);
        for (auto& bar : tr.bars)
            for (auto& beat : bar.beats)
                for (auto& n : beat.notes)
                {
                    if (! juce::isPositiveAndBelow (n.string, (int) last.size())) continue;
                    if (n.tie && last[(size_t) n.string] >= 0) n.fret = last[(size_t) n.string];
                    last[(size_t) n.string] = n.fret;
                }
    }
}
} // namespace

juce::Result readBinary (const juce::MemoryBlock& data, Song& out)
{
    try
    {
        BinaryParser p (data, out);
        p.parse();
        resolveTies (out);
    }
    catch (const FormatError& e)
    {
        return juce::Result::fail (juce::String::fromUTF8 (e.what()));
    }
    return juce::Result::ok();
}

juce::Result readGpif (const juce::String& xml, Song& out)
{
    try
    {
        GpifParser p (out);
        p.parse (xml);
        resolveTies (out);
    }
    catch (const FormatError& e)
    {
        return juce::Result::fail (juce::String::fromUTF8 (e.what()));
    }
    return juce::Result::ok();
}

juce::Result readGpx (const juce::MemoryBlock& data, Song& out)
{
    auto gpif = extractGpxFile (data, "score.gpif");
    if (gpif.isEmpty())
        return juce::Result::fail ("Could not find the score inside the Guitar Pro 6 file.");
    out.format = "Guitar Pro 6";
    return readGpif (gpif.toString(), out);
}

bool isGuitarProFile (const juce::File& f)
{
    return f.hasFileExtension ("gp3;gp4;gp5;gpx;gp");
}

juce::Result readFile (const juce::File& file, Song& out)
{
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb) || mb.getSize() < 4)
        return juce::Result::fail ("Could not read " + file.getFileName() + ".");
    if (mb.getSize() > 64 * 1024 * 1024)
        return juce::Result::fail ("The file is too large to be a Guitar Pro score.");

    const auto* d = static_cast<const char*> (mb.getData());
    if (d[0] == 'P' && d[1] == 'K')                          // GP7+: zip archive
    {
        juce::MemoryInputStream mis (mb, false);
        juce::ZipFile zip (mis);
        const int idx = zip.getIndexOfFileName ("Content/score.gpif");
        if (idx < 0) return juce::Result::fail ("This archive is not a Guitar Pro 7/8 file (no Content/score.gpif).");
        std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (idx));
        if (in == nullptr) return juce::Result::fail ("Could not unpack the Guitar Pro file.");
        out.format = "Guitar Pro 7+";
        return readGpif (in->readEntireStreamAsString(), out);
    }
    if (std::memcmp (d, "BCFZ", 4) == 0 || std::memcmp (d, "BCFS", 4) == 0)
        return readGpx (mb, out);
    return readBinary (mb, out);
}

std::vector<int> playbackOrder (const std::vector<MasterBar>& bars, int maxBars)
{
    std::vector<int> order;
    const int n = (int) bars.size();
    int start = 0, pass = 1, i = 0;
    while (i < n && (int) order.size() < maxBars)
    {
        const auto& b = bars[(size_t) i];
        if (b.repeatStart && i != start) { start = i; pass = 1; }
        const bool skip = b.alternateEndings != 0 && (b.alternateEndings & (1 << juce::jmin (15, pass - 1))) == 0;
        if (! skip) order.push_back (i);
        if (b.repeatCount > 0 && ! skip)
        {
            if (pass < b.repeatCount) { ++pass; i = start; continue; }
            pass = 1;
            start = i + 1;
        }
        else if (! skip && b.alternateEndings != 0 && (i + 1 >= n || bars[(size_t) i + 1].alternateEndings == 0))
        {
            pass = 1;              // last ending played: the repeat is finished
            start = i + 1;
        }
        ++i;
    }
    return order;
}

namespace
{
std::vector<juce::String> splitLyrics (const juce::String& text)
{
    std::vector<juce::String> out;
    juce::String cur;
    auto flush = [&] { if (cur.trim().isNotEmpty()) out.push_back (cur.trim()); cur.clear(); };
    for (auto c : text)
    {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') flush();
        else if (c == '-') { cur << "-"; flush(); }
        else if (c == '+') cur << ' ';
        else cur << juce::String::charToString (c);
    }
    flush();
    return out;
}
} // namespace

ImportResult applySong (juce::ValueTree score, const Song& song, bool replace, juce::UndoManager* um)
{
    ImportResult res;
    res.warnings = song.warnings;
    if (song.masterBars.empty())
    {
        res.result = juce::Result::fail ("The file contains no bars.");
        return res;
    }

    const auto order = playbackOrder (song.masterBars);
    if ((int) order.size() != (int) song.masterBars.size())
        res.warnings.add ("Repeats were written out: " + juce::String ((int) song.masterBars.size()) + " bars became "
                          + juce::String ((int) order.size()) + ".");

    // tempo in effect at the start of each written bar (changes inside a bar move to the next bar line)
    std::vector<double> writtenTempo (song.masterBars.size(), song.tempo);
    {
        double cur = song.tempo;
        bool moved = false;
        for (size_t m = 0; m < song.masterBars.size(); ++m)
        {
            if (song.masterBars[m].tempo > 0.0) cur = song.masterBars[m].tempo;
            for (auto& tr : song.tracks)
                if (m < tr.bars.size() && ! tr.bars[m].beats.empty() && tr.bars[m].beats[0].tempo > 0.0)
                    cur = tr.bars[m].beats[0].tempo;
            writtenTempo[m] = cur;
            for (auto& tr : song.tracks)
                if (m < tr.bars.size())
                    for (size_t bi = 1; bi < tr.bars[m].beats.size(); ++bi)
                        if (tr.bars[m].beats[bi].tempo > 0.0) { cur = tr.bars[m].beats[bi].tempo; moved = true; }
        }
        if (moved) res.warnings.add ("Tempo changes in the middle of a bar were moved to the next bar line.");
    }

    // tracks we can import
    std::vector<const Track*> usable;
    int drums = 0, empty = 0;
    for (auto& t : song.tracks)
    {
        if (t.drums) { ++drums; continue; }
        if (t.tuning.empty()) { ++empty; continue; }
        usable.push_back (&t);
    }
    if (drums > 0) res.warnings.add (juce::String (drums) + " drum track(s) were skipped (the Drum Studio arrives in a later milestone).");
    if (empty > 0) res.warnings.add (juce::String (empty) + " track(s) without a string tuning were skipped.");
    if (usable.empty())
    {
        res.result = juce::Result::fail ("The file has no guitar or bass tracks to import.");
        return res;
    }

    if (replace)
    {
        auto newMeasures = juce::ValueTree (ids::MEASURES);
        double prevTempo = -1.0;
        int prevKey = 999;
        for (size_t i = 0; i < order.size(); ++i)
        {
            const auto& mb = song.masterBars[(size_t) order[i]];
            juce::ValueTree m (ids::MEASURE);
            m.setProperty (ids::tsNum, mb.num, nullptr);
            m.setProperty (ids::tsDen, mb.den, nullptr);
            if (mb.marker.isNotEmpty()) m.setProperty (ids::marker, mb.marker, nullptr);
            const double t = writtenTempo[(size_t) order[i]];
            if (i > 0 && std::abs (t - prevTempo) > 1.0e-6) m.setProperty (ids::tempo, t, nullptr);
            if (mb.key != prevKey) m.setProperty (ids::key, mb.key, nullptr);
            prevTempo = t;
            prevKey = mb.key;
            newMeasures.appendChild (m, nullptr);
        }
        if (auto old = measures (score); old.isValid()) score.removeChild (old, um);
        if (auto old = tracks (score); old.isValid()) score.removeChild (old, um);
        score.appendChild (newMeasures, um);
        score.appendChild (juce::ValueTree (ids::TABTRACKS), um);
        score.setProperty (ids::title, song.title.isNotEmpty() ? song.title : juce::String ("Imported score"), um);
        score.setProperty (ids::artist, song.artist, um);
        score.setProperty (ids::tempo, writtenTempo.empty() ? song.tempo : writtenTempo[(size_t) order.front()], um);
    }
    else
    {
        while (numMeasures (score) < (int) order.size())
        {
            const auto& mb = song.masterBars[(size_t) order[(size_t) numMeasures (score)]];
            addMeasure (score, -1, mb.num, mb.den, um);
        }
        int mismatched = 0;
        for (size_t i = 0; i < order.size(); ++i)
        {
            auto m = measures (score).getChild ((int) i);
            const auto& mb = song.masterBars[(size_t) order[i]];
            if ((int) m.getProperty (ids::tsNum, 4) != mb.num || (int) m.getProperty (ids::tsDen, 4) != mb.den) ++mismatched;
        }
        if (mismatched > 0)
            res.warnings.add (juce::String (mismatched) + " bar(s) have a different time signature in the file than in the current score; "
                              "the score's meter was kept.");
    }

    const int barsInScore = numMeasures (score);
    int beatsTotal = 0;
    for (auto* t : usable)
    {
        juce::ValueTree tr (ids::TABTRACK);
        tr.setProperty (ids::id, timeline::newId(), nullptr);
        tr.setProperty (ids::name, t->name.isNotEmpty() ? t->name : juce::String ("Track"), nullptr);
        tr.setProperty (ids::instrument, t->bass ? "bass" : "guitar", nullptr);
        tr.setProperty (ids::tuning, tuningToString (t->tuning), nullptr);
        tr.setProperty (ids::capo, t->capo, nullptr);
        tr.setProperty (ids::volume, (double) t->volume, nullptr);
        tr.setProperty (ids::pan, (double) t->pan, nullptr);
        if (t->mute) tr.setProperty (ids::mute, true, nullptr);
        if (t->solo) tr.setProperty (ids::solo, true, nullptr);

        const auto syllables = splitLyrics (t->lyrics);
        size_t nextSyllable = 0;
        const int ns = (int) t->tuning.size();
        for (int i = 0; i < barsInScore; ++i)
        {
            juce::ValueTree barNode (ids::BAR);
            if (i < (int) order.size() && order[(size_t) i] < (int) t->bars.size())
            {
                const int src = order[(size_t) i];
                for (auto& b : t->bars[(size_t) src].beats)
                {
                    auto beat = makeBeat (b.dur, b.notes.empty());
                    if (b.dots > 0) beat.setProperty (ids::dots, b.dots, nullptr);
                    if (b.tuplet > 0) beat.setProperty (ids::tuplet, b.tuplet, nullptr);
                    if (b.stroke != strokeNone) beat.setProperty (ids::stroke, b.stroke, nullptr);
                    if (b.tremolo) beat.setProperty (ids::tremolo, true, nullptr);
                    if (b.whammy != 0.0f) beat.setProperty (ids::whammy, (double) b.whammy, nullptr);
                    if (b.text.isNotEmpty()) beat.setProperty (ids::text, b.text, nullptr);
                    if (b.chord.isNotEmpty()) beat.setProperty (ids::chord, b.chord, nullptr);
                    if (b.chordFrets.isNotEmpty()) beat.setProperty (ids::chordFrets, b.chordFrets, nullptr);
                    juce::String lyric = b.lyric;
                    bool allTied = ! b.notes.empty();
                    for (auto& n : b.notes) allTied = allTied && n.tie;
                    if (lyric.isEmpty() && src >= t->lyricsStartBar && ! b.notes.empty() && ! allTied && nextSyllable < syllables.size())
                        lyric = syllables[nextSyllable++];
                    if (lyric.isNotEmpty()) beat.setProperty (ids::lyric, lyric, nullptr);

                    std::vector<bool> used ((size_t) ns, false);
                    for (auto& n : b.notes)
                    {
                        if (! juce::isPositiveAndBelow (n.string, ns) || used[(size_t) n.string]) continue;
                        used[(size_t) n.string] = true;
                        auto note = makeNote (n.string, n.fret);
                        auto flag = [&] (const juce::Identifier& id, bool v) { if (v) note.setProperty (id, true, nullptr); };
                        flag (ids::tie, n.tie);
                        flag (ids::hammer, n.hammer);
                        flag (ids::dead, n.dead);
                        flag (ids::ghost, n.ghost);
                        flag (ids::palmMute, n.palmMute);
                        flag (ids::letRing, n.letRing);
                        flag (ids::vibrato, n.vibrato);
                        flag (ids::staccato, n.staccato);
                        flag (ids::accent, n.accent);
                        flag (ids::tap, n.tap);
                        flag (ids::prebend, n.prebend && n.bend > 0.0f);
                        if (n.slide != slideNone) note.setProperty (ids::slide, n.slide, nullptr);
                        if (n.harmonic != harmNone) note.setProperty (ids::harmonic, n.harmonic, nullptr);
                        if (n.bend > 0.0f) note.setProperty (ids::bend, (double) juce::jmin (4.0f, n.bend), nullptr);
                        beat.appendChild (note, nullptr);
                    }
                    barNode.appendChild (beat, nullptr);
                    ++beatsTotal;
                }
            }
            tr.appendChild (barNode, nullptr);
        }
        tracks (score).appendChild (tr, um);
        ++res.tracksAdded;
    }
    juce::ignoreUnused (beatsTotal);
    return res;
}

ImportResult importFile (juce::ValueTree score, const juce::File& file, bool replace, juce::UndoManager* um)
{
    Song song;
    auto r = readFile (file, song);
    if (r.failed())
    {
        ImportResult res;
        res.result = r;
        if (song.title.trim().isEmpty()) song.title = file.getFileNameWithoutExtension();
    return res;
    }
    if (song.title.trim().isEmpty()) song.title = file.getFileNameWithoutExtension();
    return applySong (score, song, replace, um);
}
} // namespace ff::tab::gp
