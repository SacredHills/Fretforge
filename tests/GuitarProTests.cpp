#include <juce_core/juce_core.h>
#include "core/GuitarPro.h"
#include "core/TabModel.h"
#include "core/MusicXml.h"
#include "core/DrumModel.h"

using namespace ff;
namespace t = ff::tab;
namespace g = ff::tab::gp;

namespace
{
juce::File fixture (const juce::String& name)
{
    return juce::File (FF_TEST_DATA_DIR).getChildFile (name);
}

// A small GPIF document in the layout Guitar Pro 6/7 write (two bars, one guitar track, a tempo change,
// a repeat, techniques and a drum track that must be skipped).
const char* const gpifText = R"(<?xml version="1.0" encoding="utf-8"?>
<GPIF>
<GPVersion>7</GPVersion>
<Score><Title><![CDATA[Gpif Song]]></Title><Artist><![CDATA[Someone]]></Artist></Score>
<MasterTrack>
 <Tracks>0 1</Tracks>
 <Automations>
  <Automation><Type>Tempo</Type><Linear>false</Linear><Bar>0</Bar><Position>0</Position><Value>90 2</Value></Automation>
  <Automation><Type>Tempo</Type><Linear>false</Linear><Bar>1</Bar><Position>0</Position><Value>60 1</Value></Automation>
 </Automations>
</MasterTrack>
<Tracks>
 <Track id="0">
  <Name><![CDATA[Rhythm]]></Name>
  <InstrumentSet><Name>Electric Guitar</Name><Type>electricGuitar</Type></InstrumentSet>
  <Staves><Staff><Properties>
   <Property name="CapoFret"><Fret>2</Fret></Property>
   <Property name="Tuning"><Pitches>38 45 50 55 59 64</Pitches></Property>
   <Property name="DiagramCollection"><Items>
     <Item id="0" name="Dsus"><Diagram stringCount="6" fretCount="5" baseFret="0">
      <Fret string="2" fret="0"/><Fret string="3" fret="2"/><Fret string="4" fret="3"/><Fret string="5" fret="0"/>
     </Diagram></Item>
   </Items></Property>
  </Properties></Staff></Staves>
  <Lyrics dispatched="true"><Line><Text><![CDATA[hel-lo world]]></Text><Offset>0</Offset></Line></Lyrics>
 </Track>
 <Track id="1">
  <Name><![CDATA[Drums]]></Name>
  <InstrumentSet><Name>Drums</Name><Type>drumKit</Type></InstrumentSet>
 </Track>
</Tracks>
<MasterBars>
 <MasterBar><Key><AccidentalCount>2</AccidentalCount><Mode>Major</Mode></Key><Time>4/4</Time><Repeat start="true" end="false" count="0"/><Section><Letter><![CDATA[A]]></Letter><Text><![CDATA[Verse]]></Text></Section><Bars>0 2</Bars></MasterBar>
 <MasterBar><Key><AccidentalCount>2</AccidentalCount><Mode>Major</Mode></Key><Time>3/4</Time><Repeat start="false" end="true" count="2"/><Bars>1 3</Bars></MasterBar>
</MasterBars>
<Bars>
 <Bar id="0"><Clef>G2</Clef><Voices>0 -1 -1 -1</Voices></Bar>
 <Bar id="1"><Clef>G2</Clef><Voices>1 3 -1 -1</Voices></Bar>
 <Bar id="2"><Clef>Neutral</Clef><Voices>2 -1 -1 -1</Voices></Bar>
 <Bar id="3"><Clef>Neutral</Clef><Voices>2 -1 -1 -1</Voices></Bar>
</Bars>
<Voices>
 <Voice id="0"><Beats>0 1 2</Beats></Voice>
 <Voice id="1"><Beats>3 4</Beats></Voice>
 <Voice id="2"><Beats>5</Beats></Voice>
 <Voice id="3"><Beats>6</Beats></Voice>
</Voices>
<Beats>
 <Beat id="0"><Rhythm ref="0"/><Chord>0</Chord><Notes>0 1</Notes></Beat>
 <Beat id="1"><Rhythm ref="1"/><Notes>2</Notes><Properties><Property name="PickStroke"><Direction>Up</Direction></Property></Properties></Beat>
 <Beat id="2"><Rhythm ref="2"/></Beat>
 <Beat id="3"><GraceNotes>BeforeBeat</GraceNotes><Rhythm ref="3"/><Notes>3</Notes></Beat>
 <Beat id="4"><Rhythm ref="4"/><Notes>4</Notes><Tremolo>1/8</Tremolo></Beat>
 <Beat id="5"><Rhythm ref="5"/><Notes>5</Notes></Beat>
 <Beat id="6"><Rhythm ref="4"/><Notes>6</Notes></Beat>
</Beats>
<Notes>
 <Note id="0"><Properties><Property name="String"><String>0</String></Property><Property name="Fret"><Fret>0</Fret></Property></Properties></Note>
 <Note id="1"><Accent>4</Accent><Properties><Property name="String"><String>3</String></Property><Property name="Fret"><Fret>2</Fret></Property><Property name="HopoOrigin"><Enable/></Property></Properties></Note>
 <Note id="2"><LetRing/><Vibrato>Slight</Vibrato><Properties><Property name="String"><String>4</String></Property><Property name="Fret"><Fret>3</Fret></Property>
   <Property name="Bended"><Enable/></Property><Property name="BendOriginValue"><Float>0</Float></Property><Property name="BendMiddleValue"><Float>50</Float></Property><Property name="BendDestinationValue"><Float>100</Float></Property></Properties></Note>
 <Note id="3"><Properties><Property name="String"><String>2</String></Property><Property name="Fret"><Fret>4</Fret></Property></Properties></Note>
 <Note id="4"><Tie origin="false" destination="true"/><Properties><Property name="String"><String>5</String></Property><Property name="Fret"><Fret>12</Fret></Property>
   <Property name="HarmonicType"><HType>Natural</HType></Property><Property name="Slide"><Flags>2</Flags></Property><Property name="PalmMuted"><Enable/></Property></Properties></Note>
 <Note id="5"><Properties><Property name="Element"><Element>0</Element></Property><Property name="Variation"><Variation>0</Variation></Property></Properties></Note>
 <Note id="6"><Properties><Property name="String"><String>1</String></Property><Property name="Fret"><Fret>5</Fret></Property></Properties></Note>
</Notes>
<Rhythms>
 <Rhythm id="0"><NoteValue>Quarter</NoteValue></Rhythm>
 <Rhythm id="1"><NoteValue>Quarter</NoteValue><AugmentationDot count="1"/></Rhythm>
 <Rhythm id="2"><NoteValue>Eighth</NoteValue><PrimaryTuplet num="3" den="2"/></Rhythm>
 <Rhythm id="3"><NoteValue>32nd</NoteValue></Rhythm>
 <Rhythm id="4"><NoteValue>Half</NoteValue><AugmentationDot count="1"/></Rhythm>
 <Rhythm id="5"><NoteValue>Whole</NoteValue></Rhythm>
</Rhythms>
</GPIF>)";

/** Builds an uncompressed GPX ("BCFS") container holding one file. */
juce::MemoryBlock makeBcfs (const juce::String& name, const juce::MemoryBlock& content)
{
    constexpr int sector = 0x1000;
    const int dataSectors = (int) ((content.getSize() + sector - 1) / sector);
    juce::MemoryBlock fs ((size_t) sector * (size_t) (2 + dataSectors), true);
    auto* d = static_cast<juce::uint8*> (fs.getData());
    auto put32 = [] (juce::uint8* p, int v) { p[0] = (juce::uint8) v; p[1] = (juce::uint8) (v >> 8); p[2] = (juce::uint8) (v >> 16); p[3] = (juce::uint8) (v >> 24); };
    juce::uint8* entry = d + sector;
    put32 (entry, 2);
    for (int i = 0; i < name.length(); ++i) entry[4 + i] = (juce::uint8) name[i];
    put32 (entry + 0x8c, (int) content.getSize());
    for (int s = 0; s < dataSectors; ++s)
        put32 (entry + 0x94 + 4 * s, 2 + s);
    std::memcpy (d + 2 * sector, content.getData(), content.getSize());
    juce::MemoryBlock out ("BCFS", 4);
    out.append (fs.getData(), fs.getSize());
    return out;
}

juce::ValueTree noteAt (const juce::ValueTree& track, int bar, int beat, int string)
{
    return t::noteOnString (track.getChild (bar).getChild (beat), string);
}
} // namespace

struct GuitarProTests : juce::UnitTest
{
    GuitarProTests() : juce::UnitTest ("Guitar Pro import", "core") {}

    void checkFixture (const juce::String& file)
    {
        beginTest ("binary fixture " + file);
        g::Song song;
        auto r = g::readFile (fixture (file), song);
        expect (r.wasOk(), r.getErrorMessage());
        if (r.failed()) return;
        expectEquals (song.title, juce::String ("Fixture Song"));
        expectEquals (song.artist, juce::String ("FretForge"));
        expectEquals (song.tempo, 100.0);
        expectEquals ((int) song.masterBars.size(), 4);
        expectEquals ((int) song.tracks.size(), 2);
        expect (song.masterBars[1].repeatStart);
        expectEquals (song.masterBars[2].repeatCount, 2);
        expectEquals (song.masterBars[0].marker, juce::String ("Intro"));
        if (! file.endsWith ("gp3")) expectEquals (song.masterBars[0].key, 2);

        const auto& lead = song.tracks[0];
        expectEquals (lead.name, juce::String ("Lead"));
        expect (lead.tuning == std::vector<int> ({ 40, 45, 50, 55, 59, 64 }));
        expect (! lead.bass);
        expect (song.tracks[1].bass);
        expect (song.tracks[1].tuning == std::vector<int> ({ 28, 33, 38, 43 }));
        expectEquals ((int) lead.bars.size(), 4);
        expectEquals ((int) lead.bars[0].beats.size(), 4);
        expectEquals ((int) lead.bars[1].beats.size(), 6);

        const auto& b0 = lead.bars[0].beats;
        expectEquals (b0[0].notes[0].string, 0);   // GP string 6 = lowest
        expectEquals (b0[0].notes[0].fret, 3);
        expect (b0[1].notes[0].hammer);
        expectEquals (b0[3].notes[0].string, 3);
        expectWithinAbsoluteError (b0[3].notes[0].bend, 2.0f, 0.01f);
        expect (b0[3].notes[0].vibrato);
        const auto& b1 = lead.bars[1].beats;
        expectEquals (b1[0].dur, 8);
        if (! file.endsWith ("gp3")) expect (b1[0].notes[0].palmMute);   // GP3 has no palm mute
        expect (b1[4].notes[0].dead);
        expectEquals ((int) b1[5].notes.size(), 3);
        const auto& b2 = lead.bars[2].beats;
        expectEquals (b2[0].dur, 2);
        expectEquals (b2[0].tempo, 140.0);
        expectEquals (b2[0].notes[0].slide, (int) t::slideTo);
        expectEquals (b2[2].notes[0].harmonic, (int) t::harmNatural);
        expect (lead.bars[3].beats[0].notes[0].letRing);
        expect (lead.bars[3].beats[1].notes[0].tie);

        beginTest ("apply " + file);
        juce::ValueTree project ("PROJECT");
        auto score = t::getOrCreateScore (project);
        juce::UndoManager um;
        auto res = g::applySong (score, song, true, &um);
        expect (res.result.wasOk(), res.result.getErrorMessage());
        expectEquals (res.tracksAdded, 2);
        // repeat bars 2-3 written out: 1 2 3 2 3 4
        expectEquals (t::numMeasures (score), 6);
        auto ms = t::measures (score);
        expectEquals ((double) score[t::ids::tempo], 100.0);
        expectEquals ((double) ms.getChild (3)[t::ids::tempo], 100.0); // back to the repeat start: tempo restored
        expectEquals ((double) ms.getChild (4)[t::ids::tempo], 140.0);
        expect (! ms.getChild (2).hasProperty (t::ids::tempo) || (double) ms.getChild (2)[t::ids::tempo] == 140.0);
        auto lead2 = t::tracks (score).getChild (0);
        expectEquals (lead2.getChild (3).getNumChildren(), 6);        // second pass of bar 2
        expect ((bool) noteAt (lead2, 0, 1, 1)[t::ids::hammer]);
        expectEquals ((double) noteAt (lead2, 0, 3, 3)[t::ids::bend], 2.0);
        expectEquals (t::tracks (score).getChild (1)[t::ids::instrument].toString(), juce::String ("bass"));
        // compile still works and plays at 140 BPM from the right bar
        const auto map = t::tempoMap (score);
        const auto starts = t::measureStarts (score);
        expectWithinAbsoluteError (map.secondsAt (starts[2]), 8 * 0.6, 1e-9);
        expectWithinAbsoluteError (map.secondsAt (starts[3]) - map.secondsAt (starts[2]), 4 * 60.0 / 140.0, 1e-9);
        um.undoCurrentTransactionOnly();
    }

    void runTest() override
    {
        checkFixture ("fixture.gp3");
        checkFixture ("fixture.gp4");
        checkFixture ("fixture.gp5");

        beginTest ("repeat expansion with alternate endings");
        {
            std::vector<g::MasterBar> bars (6);
            bars[1].repeatStart = true;
            bars[2].alternateEndings = 1;     // 1st ending
            bars[2].repeatCount = 2;
            bars[3].alternateEndings = 2;     // 2nd ending
            bars[4].repeatCount = 2;          // implicit start after the endings
            const auto order = g::playbackOrder (bars);
            expect (order == std::vector<int> ({ 0, 1, 2, 1, 3, 4, 4, 5 }), "order");
            std::vector<g::MasterBar> loop (2);
            loop[0].repeatStart = true;
            loop[1].repeatCount = 1000000;
            expectEquals ((int) g::playbackOrder (loop, 100).size(), 100);
        }

        beginTest ("GPIF (Guitar Pro 6/7)");
        g::Song song;
        auto r = g::readGpif (gpifText, song);
        expect (r.wasOk(), r.getErrorMessage());
        expectEquals (song.title, juce::String ("Gpif Song"));
        expectEquals (song.tempo, 90.0);
        expectEquals ((int) song.masterBars.size(), 2);
        expectEquals (song.masterBars[1].tempo, 30.0);                // 60 per eighth = 30 per quarter
        expectEquals (song.masterBars[1].num, 3);
        expectEquals (song.masterBars[0].key, 2);
        expectEquals (song.masterBars[0].marker, juce::String ("Verse"));
        expectEquals (song.masterBars[1].repeatCount, 2);
        expectEquals ((int) song.tracks.size(), 2);
        const auto& tr = song.tracks[0];
        expect (tr.tuning == std::vector<int> ({ 38, 45, 50, 55, 59, 64 }));
        expectEquals (tr.capo, 2);
        expect (song.tracks[1].drums);
        expectEquals ((int) tr.bars[0].beats.size(), 3);
        const auto& bb = tr.bars[0].beats;
        expectEquals (bb[0].chord, juce::String ("Dsus"));
        expectEquals ((int) bb[0].notes.size(), 2);
        expect (bb[0].notes[1].hammer && bb[0].notes[1].accent);
        expectEquals (bb[1].dots, 1);
        expectEquals (bb[1].stroke, (int) t::strokeUp);
        expect (bb[1].notes[0].letRing && bb[1].notes[0].vibrato);
        expectWithinAbsoluteError (bb[1].notes[0].bend, 2.0f, 1e-6f);
        expectEquals (bb[2].tuplet, 3);
        expect (bb[2].notes.empty());
        expectEquals ((int) tr.bars[1].beats.size(), 1);              // grace beat dropped, voice 2 dropped
        const auto& n = tr.bars[1].beats[0].notes[0];
        expect (n.tie && n.palmMute && n.slide == t::slideTo && n.harmonic == t::harmNatural);
        expect (tr.bars[1].beats[0].tremolo);
        expect (song.warnings.joinIntoString ("\n").contains ("grace"));
        expect (song.warnings.joinIntoString ("\n").contains ("voice"));

        beginTest ("GPIF apply: lyrics, chords, repeats, drums skipped");
        {
            juce::ValueTree project ("PROJECT");
            auto score = t::getOrCreateScore (project);
            auto res = g::applySong (score, song, true, nullptr);
            expect (res.result.wasOk());
            expectEquals (res.tracksAdded, 1);
            expect (res.warnings.joinIntoString ("\n").contains ("Drum Studio"));
            auto drumsNode = ff::drums::find (project);
            expect (drumsNode.isValid());
            expectEquals (ff::drums::patterns (drumsNode).getNumChildren(), 1, "drums come in as one pattern");
            expectEquals (ff::drums::song (drumsNode).getNumChildren(), 1);
            auto dc = ff::drums::compile (drumsNode, {}, 1);
            expectEquals ((int) dc.bars.size(), 4, "drum song follows the written-out repeats");
            expectEquals ((int) dc.events.size(), 4, "one kick per bar");
            expectEquals (dc.events[0].piece, (int) ff::drums::kick2);
            expectEquals (ff::tab::gp::gp6DrumNote (10, 2), 46);
            expectEquals (ff::tab::gp::gp6DrumNote (15, 2), 53);
            expectEquals (t::numMeasures (score), 4);                  // 1 2 1 2
            auto tr0 = t::tracks (score).getChild (0);
            expectEquals (tr0.getChild (0).getChild (0)[t::ids::lyric].toString(), juce::String ("hel-"));
            expectEquals (tr0.getChild (0).getChild (1)[t::ids::lyric].toString(), juce::String ("lo"));
            expectEquals (tr0.getChild (0).getChild (0)[t::ids::chord].toString(), juce::String ("Dsus"));
            expectEquals (tr0.getChild (0).getChild (0)[t::ids::chordFrets].toString(), juce::String ("x x 0 2 3 0"));
            expectEquals ((int) tr0[t::ids::capo], 2);
            expectEquals (t::measureKey (score, 3), 2);
            expectEquals (t::measureTempo (score, 1), 30.0);
            expectEquals (t::measureTempo (score, 2), 90.0);
        }

        beginTest ("GP7 zip and GP6 container");
        {
            auto dir = juce::File::createTempFile ("gptest");
            dir.createDirectory();
            juce::MemoryBlock xml (gpifText, std::strlen (gpifText));

            juce::ZipFile::Builder zb;
            zb.addEntry (new juce::MemoryInputStream (xml, true), 9, "Content/score.gpif", juce::Time::getCurrentTime());
            zb.addEntry (new juce::MemoryInputStream (juce::MemoryBlock ("7.0", 3), true), 9, "VERSION", juce::Time::getCurrentTime());
            auto gp7 = dir.getChildFile ("song.gp");
            {
                juce::FileOutputStream os (gp7);
                zb.writeToStream (os, nullptr);
            }
            g::Song s7;
            expect (g::readFile (gp7, s7).wasOk());
            expectEquals (s7.title, juce::String ("Gpif Song"));
            expectEquals (s7.format, juce::String ("Guitar Pro 7+"));

            auto gpx = dir.getChildFile ("song.gpx");
            gpx.replaceWithData (makeBcfs ("score.gpif", xml).getData(), makeBcfs ("score.gpif", xml).getSize());
            g::Song s6;
            auto r6 = g::readFile (gpx, s6);
            expect (r6.wasOk(), r6.getErrorMessage());
            expectEquals ((int) s6.tracks.size(), 2);
            expectEquals (s6.format, juce::String ("Guitar Pro 6"));
            dir.deleteRecursively();
        }

        beginTest ("rejects garbage and truncated files");
        {
            g::Song s;
            juce::MemoryBlock junk ("hello world, not a guitar pro file at all.......", 48);
            expect (g::readBinary (junk, s).failed());
            juce::MemoryBlock gp5;
            fixture ("fixture.gp5").loadFileAsData (gp5);
            for (size_t cut : { (size_t) 10, (size_t) 200, gp5.getSize() / 2, gp5.getSize() - 40 })
            {
                juce::MemoryBlock part (gp5.getData(), cut);
                g::Song s2;
                expect (g::readBinary (part, s2).failed(), "truncated at " + juce::String ((int) cut));
            }
            g::Song s3;
            expect (g::readGpif ("<NotGpif/>", s3).failed());
            expect (g::extractGpxFile (junk, "score.gpif").isEmpty());
        }

        // Optional: sweep a folder of real-world files (set FF_GP_TESTDATA to a directory).
        const auto dirName = juce::SystemStats::getEnvironmentVariable ("FF_GP_TESTDATA", {});
        if (dirName.isNotEmpty())
        {
            beginTest ("external Guitar Pro files in " + dirName);
            int ok = 0, total = 0;
            for (const auto& entry : juce::RangedDirectoryIterator (juce::File (dirName), true, "*.gp3;*.gp4;*.gp5;*.gpx;*.gp"))
            {
                ++total;
                g::Song s;
                auto res = g::readFile (entry.getFile(), s);
                if (res.wasOk() && juce::SystemStats::getEnvironmentVariable ("FF_GP_DUMPDIR", {}).isNotEmpty())
                {
                    // text dump for cross-checking against other readers: track | bar | dur.dots.tuplet:string/fret,...
                    juce::String dump;
                    for (auto& tr : s.tracks)
                    {
                        dump << "T " << tr.name << " " << (tr.drums ? "drums" : "") << "\n";
                        for (size_t b = 0; b < tr.bars.size(); ++b)
                        {
                            dump << (int) b << ":";
                            for (auto& bt : tr.bars[b].beats)
                            {
                                dump << " " << bt.dur << "." << bt.dots << "." << bt.tuplet << ":";
                                for (auto& n : bt.notes) dump << n.string << "/" << n.fret << ",";
                            }
                            dump << "\n";
                        }
                    }
                    juce::File (juce::SystemStats::getEnvironmentVariable ("FF_GP_DUMPDIR", {}))
                        .getChildFile (entry.getFile().getFileName() + ".txt").replaceWithText (dump);
                    {
                        juce::ValueTree p ("PROJECT");
                        auto sc = t::getOrCreateScore (p);
                        if (g::applySong (sc, s, true, nullptr).result.wasOk())
                            ff::tab::musicxml::exportFile (sc, juce::File (juce::SystemStats::getEnvironmentVariable ("FF_GP_DUMPDIR", {}))
                                                                   .getChildFile (entry.getFile().getFileName() + ".musicxml"));
                    }
                }
                if (res.wasOk())
                {
                    juce::ValueTree project ("PROJECT");
                    auto score = t::getOrCreateScore (project);
                    auto applied = g::applySong (score, s, true, nullptr);
                    if (applied.result.wasOk() || s.tracks.empty() || applied.result.getErrorMessage().contains ("no guitar"))
                    {
                        ++ok;
                        // every bar must be readable and the score must compile
                        t::compile (score);
                        logMessage ("  ok   " + entry.getFile().getFileName() + "  " + s.format + ", " + juce::String ((int) s.masterBars.size())
                                    + " bars, " + juce::String ((int) s.tracks.size()) + " tracks" + (applied.warnings.isEmpty() ? juce::String() : "  [" + applied.warnings.joinIntoString ("; ") + "]"));
                        continue;
                    }
                    res = applied.result;
                }
                logMessage ("  FAIL " + entry.getFile().getFileName() + ": " + res.getErrorMessage());
            }
            logMessage (juce::String (ok) + " / " + juce::String (total) + " files imported");
            expectEquals (ok, total);
        }
    }
};

static GuitarProTests guitarProTests;
