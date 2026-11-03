#include <juce_core/juce_core.h>
#include "core/MusicXml.h"
#include "core/TabModel.h"

using namespace ff;
namespace t = ff::tab;
namespace mx = ff::tab::musicxml;

namespace
{
juce::ValueTree addBeat (juce::ValueTree bar, int dur, std::initializer_list<std::pair<int, int>> notes)
{
    auto b = t::makeBeat (dur, notes.size() == 0);
    for (auto [s, f] : notes) b.appendChild (t::makeNote (s, f), nullptr);
    bar.appendChild (b, nullptr);
    return b;
}

const char* const notationOnly = R"(<?xml version="1.0" encoding="UTF-8"?>
<score-partwise version="3.1">
  <movement-title>Melody</movement-title>
  <part-list><score-part id="P1"><part-name>Flute-ish</part-name></score-part></part-list>
  <part id="P1">
    <measure number="1">
      <attributes><divisions>2</divisions><key><fifths>-1</fifths></key><time><beats>3</beats><beat-type>4</beat-type></time></attributes>
      <direction><sound tempo="72"/></direction>
      <barline location="left"><repeat direction="forward"/></barline>
      <harmony><root><root-step>F</root-step></root><kind>major</kind></harmony>
      <note><pitch><step>F</step><octave>4</octave></pitch><duration>2</duration><voice>1</voice><type>quarter</type><lyric><syllabic>begin</syllabic><text>Hap</text></lyric></note>
      <note><pitch><step>A</step><octave>4</octave></pitch><duration>2</duration><voice>1</voice><type>quarter</type>
        <notations><articulations><staccato/></articulations></notations></note>
      <note><chord/><pitch><step>C</step><octave>5</octave></pitch><duration>2</duration><voice>1</voice><type>quarter</type></note>
      <note><pitch><step>B</step><alter>-1</alter><octave>4</octave></pitch><duration>1</duration><voice>1</voice><type>eighth</type></note>
      <note><rest/><duration>1</duration><voice>1</voice><type>eighth</type></note>
      <backup><duration>6</duration></backup>
      <note><pitch><step>F</step><octave>3</octave></pitch><duration>6</duration><voice>2</voice><type>half</type><dot/></note>
    </measure>
    <measure number="2">
      <note><rest measure="yes"/><duration>6</duration><voice>1</voice></note>
      <barline location="right"><repeat direction="backward" times="2"/></barline>
    </measure>
    <measure number="3">
      <note><pitch><step>G</step><octave>4</octave></pitch><duration>6</duration><tie type="start"/><voice>1</voice><type>half</type><dot/></note>
    </measure>
  </part>
</score-partwise>)";
} // namespace

struct MusicXmlTests : juce::UnitTest
{
    MusicXmlTests() : juce::UnitTest ("MusicXML", "core") {}

    void runTest() override
    {
        beginTest ("pitch spelling follows the key");
        expect (mx::spell (61, 2).step == 'C' && mx::spell (61, 2).alter == 1);
        expect (mx::spell (61, -3).step == 'D' && mx::spell (61, -3).alter == -1);
        expectEquals (mx::spell (60, 0).octave, 4);
        expectEquals (mx::spell (40, 0).octave, 2);
        expect (mx::spell (63, 0).step == 'E' && mx::spell (63, 0).alter == -1);   // Eb in C
        expect (mx::spell (66, 0).step == 'F' && mx::spell (66, 0).alter == 1);    // F# in C

        beginTest ("export / import round trip keeps fingering, rhythm, techniques, tempo, key, chords and lyrics");
        juce::ValueTree project ("PROJECT");
        auto score = t::getOrCreateScore (project);
        score.setProperty (t::ids::title, "Round Trip", nullptr);
        score.setProperty (t::ids::tempo, 96.0, nullptr);
        while (t::numMeasures (score) > 3) t::removeMeasure (score, 3, nullptr);
        t::measures (score).getChild (0).setProperty (t::ids::key, 1, nullptr);
        t::measures (score).getChild (1).setProperty (t::ids::tempo, 150.0, nullptr);
        t::measures (score).getChild (1).setProperty (t::ids::marker, "Chorus", nullptr);
        t::measures (score).getChild (2).setProperty (t::ids::tsNum, 3, nullptr);
        auto gtr = t::addTrack (score, "Lead", { 38, 45, 50, 55, 59, 64 }, "guitar", nullptr);
        gtr.setProperty (t::ids::capo, 2, nullptr);
        auto bassT = t::addTrack (score, "Bass", { 28, 33, 38, 43 }, "bass", nullptr);

        auto b0 = t::bar (gtr, 0);
        auto c = addBeat (b0, 4, { { 0, 0 }, { 3, 2 }, { 4, 3 }, { 5, 2 } });
        c.setProperty (t::ids::chord, "Dsus2", nullptr);
        c.setProperty (t::ids::lyric, "sing-", nullptr);
        auto h = addBeat (b0, 8, { { 2, 5 } });
        h.getChild (0).setProperty (t::ids::hammer, true, nullptr);
        addBeat (b0, 8, { { 2, 7 } }).setProperty (t::ids::lyric, "ing", nullptr);
        auto bn = addBeat (b0, 8, { { 3, 7 } });
        bn.setProperty (t::ids::dots, 1, nullptr);
        bn.getChild (0).setProperty (t::ids::bend, 2.0, nullptr);
        bn.getChild (0).setProperty (t::ids::vibrato, true, nullptr);
        addBeat (b0, 16, {});
        addBeat (b0, 4, { { 1, 12 } }).getChild (0).setProperty (t::ids::harmonic, (int) t::harmNatural, nullptr);
        auto b1 = t::bar (gtr, 1);
        for (int i = 0; i < 3; ++i)
        {
            auto tb = addBeat (b1, 8, { { 0, i } });
            tb.setProperty (t::ids::tuplet, 3, nullptr);
            tb.getChild (0).setProperty (t::ids::palmMute, true, nullptr);
        }
        addBeat (b1, 4, { { 5, 0 } }).getChild (0).setProperty (t::ids::dead, true, nullptr);
        addBeat (b1, 2, { { 4, 5 } }).getChild (0).setProperty (t::ids::slide, (int) t::slideTo, nullptr);
        addBeat (t::bar (gtr, 2), 2, { { 4, 7 } });
        auto tied = addBeat (t::bar (gtr, 2), 4, { { 4, 7 } });
        tied.getChild (0).setProperty (t::ids::tie, true, nullptr);
        addBeat (t::bar (bassT, 0), 1, { { 0, 0 } });
        // bass bars 2 and 3 left empty on purpose

        auto file = juce::File::createTempFile ("musicxml");
        auto wr = mx::exportFile (score, file);
        expect (wr.wasOk(), wr.getErrorMessage());
        const auto text = file.loadFileAsString();
        expect (text.contains ("<!DOCTYPE score-partwise"));
        expect (text.contains ("<sign>TAB</sign>"));

        juce::ValueTree project2 ("PROJECT");
        auto score2 = t::getOrCreateScore (project2);
        auto res = mx::importFile (score2, file, true, nullptr);
        expect (res.result.wasOk(), res.result.getErrorMessage());
        expectEquals (res.tracksAdded, 2);
        expectEquals (score2[t::ids::title].toString(), juce::String ("Round Trip"));
        expectEquals ((double) score2[t::ids::tempo], 96.0);
        expectEquals (t::numMeasures (score2), 3);
        expectEquals (t::measureTempo (score2, 1), 150.0);
        expectEquals (t::measureKey (score2, 0), 1);
        expectEquals ((int) t::measures (score2).getChild (2)[t::ids::tsNum], 3);
        expectEquals (t::measures (score2).getChild (1)[t::ids::marker].toString(), juce::String ("Chorus"));

        auto g2 = t::tracks (score2).getChild (0);
        expectEquals (g2[t::ids::tuning].toString(), gtr[t::ids::tuning].toString());
        expectEquals ((int) g2[t::ids::capo], 2);
        // every beat and note survives with the same string, fret and length
        for (int m = 0; m < 3; ++m)
        {
            auto a = t::bar (gtr, m), b = t::bar (g2, m);
            expectEquals (b.getNumChildren(), a.getNumChildren(), "beats in bar " + juce::String (m + 1));
            for (int i = 0; i < juce::jmin (a.getNumChildren(), b.getNumChildren()); ++i)
            {
                expectWithinAbsoluteError (t::beatLengthQ (b.getChild (i)), t::beatLengthQ (a.getChild (i)), 1e-9);
                expectEquals (b.getChild (i).getNumChildren(), a.getChild (i).getNumChildren());
                for (auto n : a.getChild (i))
                {
                    auto n2 = t::noteOnString (b.getChild (i), (int) n[t::ids::string]);
                    expect (n2.isValid(), "note on string " + n[t::ids::string].toString());
                    expectEquals ((int) n2[t::ids::fret], (int) n[t::ids::fret]);
                    for (auto id : { t::ids::hammer, t::ids::vibrato, t::ids::palmMute, t::ids::dead, t::ids::tie })
                        expect ((bool) n2[id] == (bool) n[id], id.toString());
                    expectEquals ((int) n2[t::ids::harmonic], (int) n[t::ids::harmonic]);
                    expectEquals ((int) n2[t::ids::slide], (int) n[t::ids::slide]);
                    expectEquals ((double) n2[t::ids::bend], (double) n[t::ids::bend]);
                }
            }
        }
        expectEquals (t::bar (g2, 0).getChild (0)[t::ids::chord].toString(), juce::String ("Dsus2"));
        expectEquals (t::bar (g2, 0).getChild (0)[t::ids::lyric].toString(), juce::String ("sing-"));
        expectEquals (t::bar (g2, 0).getChild (2)[t::ids::lyric].toString(), juce::String ("ing"));
        auto bass2 = t::tracks (score2).getChild (1);
        expectEquals (bass2[t::ids::instrument].toString(), juce::String ("bass"));
        expect ((bool) t::bar (bass2, 1).getChild (0)[t::ids::rest]);   // empty bar exported as a rest
        file.deleteFile();

        beginTest ("notation-only parts get strings assigned; repeats, rests, voices");
        {
            t::gp::Song song;
            auto r = mx::readText (notationOnly, song);
            expect (r.wasOk(), r.getErrorMessage());
            expectEquals (song.tempo, 72.0);
            expectEquals (song.masterBars[0].key, -1);
            expectEquals (song.masterBars[0].num, 3);
            expect (song.masterBars[0].repeatStart);
            expectEquals (song.masterBars[1].repeatCount, 2);
            const auto& tr = song.tracks[0];
            expect (tr.tuning == std::vector<int> ({ 40, 45, 50, 55, 59, 64 }));
            const auto& beats = tr.bars[0].beats;
            expectEquals ((int) beats.size(), 4);
            expectEquals (beats[0].chord, juce::String ("F"));
            expectEquals (beats[0].lyric, juce::String ("Hap-"));
            expectEquals ((int) beats[1].notes.size(), 2);
            expect (beats[1].notes[0].staccato || beats[1].notes[1].staccato);
            auto pitchOf = [&] (const t::gp::Note& n) { return tr.tuning[(size_t) n.string] + n.fret; };
            expectEquals (pitchOf (beats[0].notes[0]), 65);  // F4
            expectEquals (pitchOf (beats[2].notes[0]), 70);  // Bb4
            expect (beats[3].notes.empty());
            expectWithinAbsoluteError (t::beatLengthQ (tr.bars[1].beats[0].dur, tr.bars[1].beats[0].dots, 0), 3.0, 1e-9);
            expect (song.warnings.joinIntoString (" ").contains ("voice"));

            juce::ValueTree p ("PROJECT");
            auto sc = t::getOrCreateScore (p);
            auto applied = t::gp::applySong (sc, song, true, nullptr);
            expect (applied.result.wasOk());
            expectEquals (t::numMeasures (sc), 5);           // 1 2 1 2 3
        }

        beginTest ("compressed .mxl");
        {
            juce::MemoryBlock xml (notationOnly, std::strlen (notationOnly));
            const char* container = R"(<?xml version="1.0"?><container><rootfiles><rootfile full-path="score/song.musicxml"/></rootfiles></container>)";
            juce::ZipFile::Builder zb;
            zb.addEntry (std::make_unique<juce::MemoryInputStream> (juce::MemoryBlock (container, std::strlen (container)), true), 9,
                         "META-INF/container.xml", juce::Time::getCurrentTime());
            zb.addEntry (std::make_unique<juce::MemoryInputStream> (xml, true), 9, "score/song.musicxml", juce::Time::getCurrentTime());
            auto f = juce::File::createTempFile ("mxl");
            {
                juce::FileOutputStream os (f);
                zb.writeToStream (os, nullptr);
            }
            t::gp::Song song;
            auto r = mx::read (f, song);
            expect (r.wasOk(), r.getErrorMessage());
            expectEquals (song.title, juce::String ("Melody"));
            f.deleteFile();
        }

        beginTest ("rejects non-MusicXML");
        {
            t::gp::Song s;
            expect (mx::readText ("<html/>", s).failed());
            expect (mx::readText ("not xml", s).failed());
            expect (mx::readText ("<score-timewise/>", s).failed());
        }

        const auto dirName = juce::SystemStats::getEnvironmentVariable ("FF_MUSICXML_TESTDATA", {});
        if (dirName.isNotEmpty())
        {
            beginTest ("external MusicXML files in " + dirName);
            int ok = 0, total = 0;
            for (const auto& entry : juce::RangedDirectoryIterator (juce::File (dirName), true, "*.xml;*.musicxml;*.mxl"))
            {
                ++total;
                t::gp::Song s;
                auto r = mx::read (entry.getFile(), s);
                juce::String info;
                if (r.wasOk())
                {
                    juce::ValueTree p ("PROJECT");
                    auto sc = t::getOrCreateScore (p);
                    auto applied = t::gp::applySong (sc, s, true, nullptr);
                    if (applied.result.failed() && ! applied.result.getErrorMessage().contains ("no guitar")) r = applied.result;
                    info = juce::String ((int) s.masterBars.size()) + " bars, " + juce::String ((int) s.tracks.size()) + " parts";
                }
                if (r.wasOk()) { ++ok; logMessage ("  ok   " + entry.getFile().getFileName() + "  " + info); }
                else logMessage ("  FAIL " + entry.getFile().getFileName() + ": " + r.getErrorMessage());
            }
            logMessage (juce::String (ok) + " / " + juce::String (total) + " MusicXML files imported");
        }
    }
};

static MusicXmlTests musicXmlTests;
