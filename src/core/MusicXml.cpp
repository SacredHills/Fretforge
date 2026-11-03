#include "MusicXml.h"
#include "MusicTheory.h"
#include <algorithm>
#include <map>
#include <tuple>

namespace ff::tab::musicxml
{
namespace
{
constexpr int divisionsPerQuarter = 6720; // divisible by every duration/dot/tuplet combination we write

const char* typeName (int dur)
{
    switch (dur)
    {
        case 1: return "whole";
        case 2: return "half";
        case 4: return "quarter";
        case 8: return "eighth";
        case 16: return "16th";
        case 32: return "32nd";
        default: return "64th";
    }
}

int durFromType (const juce::String& t)
{
    if (t == "breve" || t == "long" || t == "maxima" || t == "whole") return 1;
    if (t == "half") return 2;
    if (t == "quarter") return 4;
    if (t == "eighth") return 8;
    if (t == "16th") return 16;
    if (t == "32nd") return 32;
    if (t == "64th" || t == "128th" || t == "256th" || t == "512th" || t == "1024th") return 64;
    return 0;
}

int stepToSemitone (juce::juce_wchar step)
{
    switch (juce::CharacterFunctions::toUpperCase (step))
    {
        case 'C': return 0; case 'D': return 2; case 'E': return 4; case 'F': return 5;
        case 'G': return 7; case 'A': return 9; case 'B': return 11; default: return 0;
    }
}

int pitchToMidi (const juce::XmlElement* p, const juce::String& prefix = {})
{
    if (p == nullptr) return -1;
    const auto step = p->getChildElementAllSubText (prefix + "step", "C").trim();
    const int alter = juce::roundToInt (p->getChildElementAllSubText (prefix + "alter", "0").getDoubleValue());
    const int octave = p->getChildElementAllSubText (prefix + "octave", "4").getIntValue();
    return juce::jlimit (0, 127, (octave + 1) * 12 + stepToSemitone (step.isEmpty() ? 'C' : step[0]) + alter);
}

std::vector<int> tuningForRange (int lowest, int highest, bool& bass)
{
    bass = false;
    if (lowest >= 40) return { 40, 45, 50, 55, 59, 64 };
    if (highest < 62) { bass = true; return lowest < 28 ? std::vector<int> { 23, 28, 33, 38, 43 } : std::vector<int> { 28, 33, 38, 43 }; }
    if (lowest >= 35) return { 35, 40, 45, 50, 55, 59, 64 };
    return { 30, 35, 40, 45, 50, 55, 59, 64 };
}

/** Closest (dur, dots) for a length in quarters. */
std::pair<int, int> durationForQuarters (double q)
{
    std::pair<int, int> best { 4, 0 };
    double err = 1.0e9;
    for (int dur : { 1, 2, 4, 8, 16, 32, 64 })
        for (int dots = 0; dots <= 2; ++dots)
        {
            const double e = std::abs (beatLengthQ (dur, dots, 0) - q);
            if (e < err - 1.0e-9) { err = e; best = { dur, dots }; }
        }
    return best;
}

juce::String chordNameFromHarmony (const juce::XmlElement* h)
{
    auto* root = h->getChildByName ("root");
    if (root == nullptr) return {};
    juce::String name = root->getChildElementAllSubText ("root-step", "").trim();
    const int alter = root->getChildElementAllSubText ("root-alter", "0").getIntValue();
    if (alter > 0) name << "#";
    if (alter < 0) name << "b";
    if (auto* kind = h->getChildByName ("kind"))
    {
        if (kind->hasAttribute ("text")) name << kind->getStringAttribute ("text");
        else
        {
            static const std::map<juce::String, juce::String> kinds {
                { "major", "" }, { "minor", "m" }, { "augmented", "aug" }, { "diminished", "dim" }, { "dominant", "7" },
                { "major-seventh", "maj7" }, { "minor-seventh", "m7" }, { "diminished-seventh", "dim7" }, { "half-diminished", "m7b5" },
                { "suspended-second", "sus2" }, { "suspended-fourth", "sus4" }, { "power", "5" }, { "major-sixth", "6" },
                { "minor-sixth", "m6" }, { "dominant-ninth", "9" }, { "major-ninth", "maj9" }, { "minor-ninth", "m9" } };
            auto it = kinds.find (kind->getAllSubText().trim());
            if (it != kinds.end()) name << it->second;
        }
    }
    if (auto* bass = h->getChildByName ("bass"))
    {
        name << "/" << bass->getChildElementAllSubText ("bass-step", "").trim();
        const int ba = bass->getChildElementAllSubText ("bass-alter", "0").getIntValue();
        if (ba > 0) name << "#";
        if (ba < 0) name << "b";
    }
    return name;
}

class Reader
{
public:
    explicit Reader (gp::Song& s) : song (s) {}

    void parse (const juce::String& text)
    {
        auto doc = juce::XmlDocument::parse (text);
        if (doc == nullptr)
            throw juce::String ("The file is not valid XML.");
        if (doc->hasTagName ("score-timewise"))
            throw juce::String ("Time-wise MusicXML is not supported; please export as part-wise MusicXML.");
        if (! doc->hasTagName ("score-partwise"))
            throw juce::String ("Not a MusicXML score.");
        song.format = "MusicXML";
        if (auto* work = doc->getChildByName ("work")) song.title = work->getChildElementAllSubText ("work-title", "").trim();
        if (song.title.isEmpty()) song.title = doc->getChildElementAllSubText ("movement-title", "").trim();
        if (auto* id = doc->getChildByName ("identification"))
            for (auto* c : id->getChildWithTagNameIterator ("creator"))
                if (c->getStringAttribute ("type") == "composer" || song.artist.isEmpty()) song.artist = c->getAllSubText().trim();

        std::map<juce::String, juce::String> partNames;
        if (auto* pl = doc->getChildByName ("part-list"))
            for (auto* sp : pl->getChildWithTagNameIterator ("score-part"))
                partNames[sp->getStringAttribute ("id")] = sp->getChildElementAllSubText ("part-name", "").trim();

        bool firstPart = true;
        for (auto* part : doc->getChildWithTagNameIterator ("part"))
        {
            readPart (part, partNames[part->getStringAttribute ("id")], firstPart);
            firstPart = false;
        }
        if (song.masterBars.empty()) throw juce::String ("The score has no measures.");
        for (auto& t : song.tracks) t.bars.resize (song.masterBars.size());
        if (otherVoices > 0) song.warnings.add (juce::String (otherVoices) + " note(s) in other voices or staves were left out; only voice 1 is imported.");
        if (graceNotes > 0) song.warnings.add (juce::String (graceNotes) + " grace note(s) were left out (not supported yet).");
        if (oddTuplets > 0) song.warnings.add (juce::String (oddTuplets) + " note(s) used tuplets other than 3, 5, 6 or 7 and were imported without the tuplet.");
        if (unplayable > 0) song.warnings.add (juce::String (unplayable) + " note(s) were outside the instrument's range and were left out.");
    }

private:
    gp::Song& song;
    int otherVoices = 0, graceNotes = 0, oddTuplets = 0, unplayable = 0;

    struct PendingBeat { gp::Beat beat; std::vector<int> pitches; std::vector<std::pair<int, int>> tabNotes; std::vector<gp::Note> techniques; };

    void readPart (const juce::XmlElement* part, const juce::String& name, bool definesBars)
    {
        gp::Track tr;
        tr.name = name.isNotEmpty() ? name : "Part " + juce::String ((int) song.tracks.size() + 1);
        int divisions = 1, key = 0, num = 4, den = 4;
        std::map<int, int> tuningByLine;  // line 1 = lowest string
        juce::String firstVoice;
        std::vector<std::vector<PendingBeat>> bars;
        juce::String pendingChord;
        bool isPercussion = false;

        int mIndex = 0;
        for (auto* measure : part->getChildWithTagNameIterator ("measure"))
        {
            if (definesBars && mIndex >= (int) song.masterBars.size())
            {
                gp::MasterBar mb;
                mb.num = num; mb.den = den; mb.key = key;
                song.masterBars.push_back (mb);
            }
            gp::MasterBar dummy;
            auto& mb = mIndex < (int) song.masterBars.size() ? song.masterBars[(size_t) mIndex] : dummy;
            bars.emplace_back();
            auto& beats = bars.back();

            for (auto* e = measure->getFirstChildElement(); e != nullptr; e = e->getNextElement())
            {
                const auto tag = e->getTagName();
                if (tag == "attributes")
                {
                    divisions = juce::jmax (1, e->getChildElementAllSubText ("divisions", juce::String (divisions)).getIntValue());
                    if (auto* k = e->getChildByName ("key"))
                    {
                        key = juce::jlimit (-7, 7, k->getChildElementAllSubText ("fifths", "0").getIntValue());
                        if (definesBars) { mb.key = key; mb.keyChange = true; }
                    }
                    if (auto* t = e->getChildByName ("time"))
                    {
                        num = juce::jlimit (1, 32, t->getChildElementAllSubText ("beats", "4").getIntValue());
                        den = juce::jlimit (1, 64, t->getChildElementAllSubText ("beat-type", "4").getIntValue());
                        if (definesBars) { mb.num = num; mb.den = den; }
                    }
                    if (auto* clef = e->getChildByName ("clef"))
                        if (clef->getChildElementAllSubText ("sign", "") == "percussion") isPercussion = true;
                    for (auto* sd : e->getChildWithTagNameIterator ("staff-details"))
                    {
                        if (sd->getIntAttribute ("number", 1) != 1) continue;
                        for (auto* st : sd->getChildWithTagNameIterator ("staff-tuning"))
                            tuningByLine[st->getIntAttribute ("line")] = pitchToMidi (st, "tuning-");
                        if (auto* capo = sd->getChildByName ("capo")) tr.capo = juce::jlimit (0, 24, capo->getAllSubText().getIntValue());
                    }
                }
                else if (tag == "direction" && definesBars)
                {
                    if (auto* snd = e->getChildByName ("sound"); snd != nullptr && snd->hasAttribute ("tempo"))
                    {
                        const double t = juce::jlimit (10.0, 999.0, snd->getDoubleAttribute ("tempo"));
                        if (mIndex == 0 && beats.empty()) song.tempo = t; else mb.tempo = t;
                    }
                    if (auto* dt = e->getChildByName ("direction-type"))
                        if (auto* reh = dt->getChildByName ("rehearsal")) mb.marker = reh->getAllSubText().trim();
                }
                else if (tag == "sound" && definesBars && e->hasAttribute ("tempo"))
                {
                    const double t = juce::jlimit (10.0, 999.0, e->getDoubleAttribute ("tempo"));
                    if (mIndex == 0 && beats.empty()) song.tempo = t; else mb.tempo = t;
                }
                else if (tag == "barline" && definesBars)
                {
                    if (auto* rep = e->getChildByName ("repeat"))
                    {
                        if (rep->getStringAttribute ("direction") == "forward") mb.repeatStart = true;
                        else mb.repeatCount = juce::jmax (2, rep->getIntAttribute ("times", 2));
                    }
                    if (auto* end = e->getChildByName ("ending"))
                        if (end->getStringAttribute ("type") == "start")
                            for (auto& tok : juce::StringArray::fromTokens (end->getStringAttribute ("number"), ", ", ""))
                                if (tok.getIntValue() >= 1 && tok.getIntValue() <= 16) mb.alternateEndings |= 1 << (tok.getIntValue() - 1);
                }
                else if (tag == "harmony")
                    pendingChord = chordNameFromHarmony (e);
                else if (tag == "note")
                    readNote (e, beats, divisions, firstVoice, pendingChord);
            }
            ++mIndex;
        }

        if (isPercussion) { tr.drums = true; song.tracks.push_back (tr); return; }

        // tuning: explicit TAB staff, or chosen from the part's range
        const bool tabPart = ! tuningByLine.empty();
        if (tabPart)
            for (auto& [line, midi] : tuningByLine) tr.tuning.push_back (midi);
        else
        {
            int lo = 127, hi = 0;
            for (auto& bar : bars) for (auto& b : bar) for (int p : b.pitches) { lo = juce::jmin (lo, p); hi = juce::jmax (hi, p); }
            if (lo > hi) { lo = 40; hi = 64; }
            tr.tuning = tuningForRange (lo, hi, tr.bass);
        }
        if (! tr.tuning.empty() && tr.tuning.front() <= 33 && tr.tuning.size() <= 6) tr.bass = true;
        const int ns = (int) tr.tuning.size();

        int prevFret = 3;
        for (auto& bar : bars)
        {
            gp::Bar out;
            for (auto& pb : bar)
            {
                gp::Beat b = pb.beat;
                std::vector<std::pair<int, int>> placed;
                if (tabPart && pb.tabNotes.size() == pb.pitches.size())
                    for (auto& [s, f] : pb.tabNotes) placed.push_back ({ ns - s, f });
                else if (! pb.pitches.empty())
                {
                    std::vector<int> fretted;
                    for (int p : pb.pitches) fretted.push_back (p - tr.capo);
                    placed = assignStrings (fretted, tr.tuning, prevFret);
                    unplayable += (int) pb.pitches.size() - (int) placed.size();
                    int sum = 0, cnt = 0;
                    for (auto& [s, f] : placed) if (f > 0) { sum += f; ++cnt; }
                    if (cnt > 0) prevFret = sum / cnt;
                }
                for (size_t i = 0; i < placed.size(); ++i)
                {
                    gp::Note n = i < pb.techniques.size() ? pb.techniques[i] : gp::Note();
                    if (! tabPart)
                    {
                        // techniques follow pitch order after assignment
                        const int pitch = tr.tuning[(size_t) juce::jlimit (0, ns - 1, placed[i].first)] + placed[i].second + tr.capo;
                        for (size_t k = 0; k < pb.pitches.size(); ++k)
                            if (pb.pitches[k] == pitch && k < pb.techniques.size()) { n = pb.techniques[k]; break; }
                    }
                    n.string = placed[i].first;
                    n.fret = juce::jlimit (0, maxFret, placed[i].second);
                    if (juce::isPositiveAndBelow (n.string, ns)) b.notes.push_back (n);
                }
                out.beats.push_back (b);
            }
            tr.bars.push_back (out);
        }
        song.tracks.push_back (tr);
    }

    void readNote (const juce::XmlElement* e, std::vector<PendingBeat>& beats, int divisions, juce::String& firstVoice, juce::String& pendingChord)
    {
        if (e->getChildByName ("grace") != nullptr) { ++graceNotes; return; }
        if (e->getChildByName ("cue") != nullptr) return;
        const auto voice = e->getChildElementAllSubText ("voice", "1").trim();
        if (firstVoice.isEmpty()) firstVoice = voice;
        if (voice != firstVoice || e->getChildElementAllSubText ("staff", "1").trim() != "1")
        {
            if (e->getChildByName ("rest") == nullptr) ++otherVoices;
            return;
        }

        const bool chord = e->getChildByName ("chord") != nullptr && ! beats.empty();
        if (! chord)
        {
            PendingBeat pb;
            int dur = durFromType (e->getChildElementAllSubText ("type", ""));
            int dots = 0;
            for (auto* d : e->getChildWithTagNameIterator ("dot")) { juce::ignoreUnused (d); ++dots; }
            if (dur == 0)
            {
                const double q = e->getChildElementAllSubText ("duration", "0").getDoubleValue() / divisions;
                std::tie (dur, dots) = durationForQuarters (q > 0.0 ? q : 1.0);
            }
            pb.beat.dur = dur;
            pb.beat.dots = juce::jmin (2, dots);
            if (auto* tm = e->getChildByName ("time-modification"))
            {
                const int actual = tm->getChildElementAllSubText ("actual-notes", "1").getIntValue();
                const int normal = tm->getChildElementAllSubText ("normal-notes", "1").getIntValue();
                if ((actual == 3 && normal == 2) || ((actual == 5 || actual == 6 || actual == 7) && normal == 4)) pb.beat.tuplet = actual;
                else if (actual != normal) ++oddTuplets;
            }
            if (e->getChildByName ("rest") != nullptr && e->getChildByName ("rest")->getStringAttribute ("measure") == "yes")
            {
                // whole-bar rest: as long as the bar
                const double q = e->getChildElementAllSubText ("duration", "0").getDoubleValue() / divisions;
                if (q > 0.0) { std::tie (pb.beat.dur, pb.beat.dots) = durationForQuarters (q); pb.beat.tuplet = 0; }
            }
            pb.beat.chord = pendingChord;
            pendingChord.clear();
            if (auto* lyr = e->getChildByName ("lyric"))
            {
                auto text = lyr->getChildElementAllSubText ("text", "").trim();
                const auto syl = lyr->getChildElementAllSubText ("syllabic", "single");
                if (text.isNotEmpty() && (syl == "begin" || syl == "middle")) text << "-";
                pb.beat.lyric = text;
            }
            beats.push_back (pb);
        }
        auto& pb = beats.back();
        if (e->getChildByName ("rest") != nullptr || e->getChildByName ("unpitched") != nullptr) return;
        const int midi = pitchToMidi (e->getChildByName ("pitch"));
        if (midi < 0) return;

        gp::Note n;
        int string = -1, fret = -1;
        for (auto* tie : e->getChildWithTagNameIterator ("tie"))
            if (tie->getStringAttribute ("type") == "stop") n.tie = true;
        if (auto* head = e->getChildByName ("notehead"))
        {
            if (head->getAllSubText().trim() == "x") n.dead = true;
            if (head->getStringAttribute ("parentheses") == "yes") n.ghost = true;
        }
        if (auto* nots = e->getChildByName ("notations"))
        {
            for (auto* tied : nots->getChildWithTagNameIterator ("tied"))
                if (tied->getStringAttribute ("type") == "stop") n.tie = true;
            for (auto* sl : nots->getChildWithTagNameIterator ("slide"))
                if (sl->getStringAttribute ("type") == "start") n.slide = slideTo;
            for (auto* sl : nots->getChildWithTagNameIterator ("glissando"))
                if (sl->getStringAttribute ("type") == "start") n.slide = slideTo;
            if (auto* tech = nots->getChildByName ("technical"))
            {
                if (auto* s = tech->getChildByName ("string")) string = s->getAllSubText().getIntValue();
                if (auto* f = tech->getChildByName ("fret")) fret = f->getAllSubText().getIntValue();
                for (auto* h : tech->getChildWithTagNameIterator ("hammer-on")) if (h->getStringAttribute ("type") == "start") n.hammer = true;
                for (auto* h : tech->getChildWithTagNameIterator ("pull-off")) if (h->getStringAttribute ("type") == "start") n.hammer = true;
                if (auto* bend = tech->getChildByName ("bend"))
                {
                    n.bend = (float) juce::jmax (0.0, bend->getChildElementAllSubText ("bend-alter", "0").getDoubleValue());
                    n.prebend = bend->getChildByName ("pre-bend") != nullptr;
                }
                if (auto* harm = tech->getChildByName ("harmonic"))
                    n.harmonic = harm->getChildByName ("artificial") != nullptr ? harmArtificial : harmNatural;
                if (tech->getChildByName ("tap") != nullptr) n.tap = true;
                for (auto* o : tech->getChildWithTagNameIterator ("other-technical"))
                {
                    const auto t = o->getAllSubText().trim().toLowerCase();
                    if (t == "p.m." || t == "palm mute") n.palmMute = true;
                    else if (t == "let ring") n.letRing = true;
                    else if (t == "vibrato") n.vibrato = true;
                }
            }
            if (auto* art = nots->getChildByName ("articulations"))
            {
                if (art->getChildByName ("accent") != nullptr || art->getChildByName ("strong-accent") != nullptr) n.accent = true;
                if (art->getChildByName ("staccato") != nullptr || art->getChildByName ("staccatissimo") != nullptr) n.staccato = true;
            }
            if (auto* orn = nots->getChildByName ("ornaments"))
                if (orn->getChildByName ("wavy-line") != nullptr || orn->getChildByName ("vibrato") != nullptr) n.vibrato = true;
        }
        pb.pitches.push_back (midi);
        if (string >= 1 && fret >= 0) pb.tabNotes.push_back ({ string, fret });
        pb.techniques.push_back (n);
    }
};

// ------------------------------------------------------------------ export helpers

juce::XmlElement* add (juce::XmlElement& parent, const juce::String& tag, const juce::String& text = {})
{
    auto* e = parent.createNewChildElement (tag);
    if (text.isNotEmpty()) e->addTextElement (text);
    return e;
}

int ticks (int dur, int dots, int tuplet)
{
    return juce::roundToInt (beatLengthQ (dur, dots, tuplet) * divisionsPerQuarter);
}

void addPitch (juce::XmlElement& note, int midi, int key)
{
    const auto sp = spell (midi, key);
    auto* p = add (note, "pitch");
    add (*p, "step", juce::String::charToString ((juce::juce_wchar) sp.step));
    if (sp.alter != 0) add (*p, "alter", juce::String (sp.alter));
    add (*p, "octave", juce::String (sp.octave));
}

void addHarmony (juce::XmlElement& measure, const juce::String& name)
{
    if (name.isEmpty() || ! juce::String ("ABCDEFG").containsChar (juce::CharacterFunctions::toUpperCase (name[0]))) return;
    auto* h = add (measure, "harmony");
    h->setAttribute ("print-frame", "no");
    auto* root = add (*h, "root");
    add (*root, "root-step", juce::String::charToString (juce::CharacterFunctions::toUpperCase (name[0])));
    int i = 1;
    if (name.length() > 1 && (name[1] == '#' || name[1] == 'b'))
    {
        add (*root, "root-alter", name[1] == '#' ? "1" : "-1");
        i = 2;
    }
    auto rest = name.substring (i);
    auto* kind = add (*h, "kind", "major");
    kind->setAttribute ("text", rest);
}
} // namespace

Spelling spell (int midi, int key)
{
    static const char sharpSteps[12] = { 'C', 'C', 'D', 'D', 'E', 'F', 'F', 'G', 'G', 'A', 'A', 'B' };
    static const int sharpAlter[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
    static const char flatSteps[12] = { 'C', 'D', 'D', 'E', 'E', 'F', 'G', 'G', 'A', 'A', 'B', 'B' };
    static const int flatAlter[12] = { 0, -1, 0, -1, 0, 0, -1, 0, -1, 0, -1, 0 };
    const int pc = ((midi % 12) + 12) % 12;
    Spelling s;
    s.octave = midi / 12 - 1;
    if (key < 0) { s.step = flatSteps[pc]; s.alter = flatAlter[pc]; }
    else if (key > 0) { s.step = sharpSteps[pc]; s.alter = sharpAlter[pc]; }
    else
    {
        // C major / A minor: the usual spellings C#, Eb, F#, Ab, Bb
        const bool flat = pc == 3 || pc == 8 || pc == 10;
        s.step = flat ? flatSteps[pc] : sharpSteps[pc];
        s.alter = flat ? flatAlter[pc] : sharpAlter[pc];
    }
    return s;
}

juce::Result readText (const juce::String& xml, gp::Song& out)
{
    try
    {
        Reader r (out);
        r.parse (xml);
    }
    catch (const juce::String& message)
    {
        return juce::Result::fail (message);
    }
    return juce::Result::ok();
}

juce::Result read (const juce::File& file, gp::Song& out)
{
    if (file.getSize() > 64 * 1024 * 1024) return juce::Result::fail ("The file is too large.");
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb) || mb.getSize() < 4) return juce::Result::fail ("Could not read " + file.getFileName() + ".");
    const auto* d = static_cast<const char*> (mb.getData());
    if (d[0] == 'P' && d[1] == 'K')
    {
        juce::MemoryInputStream mis (mb, false);
        juce::ZipFile zip (mis);
        int idx = -1;
        // META-INF/container.xml names the root file; otherwise take the first score in the archive
        const int ci = zip.getIndexOfFileName ("META-INF/container.xml");
        if (ci >= 0)
            if (std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (ci)); in != nullptr)
                if (auto container = juce::XmlDocument::parse (in->readEntireStreamAsString()))
                    if (auto* rf = container->getChildByName ("rootfiles"))
                        if (auto* r = rf->getChildByName ("rootfile"))
                            idx = zip.getIndexOfFileName (r->getStringAttribute ("full-path"));
        for (int i = 0; idx < 0 && i < zip.getNumEntries(); ++i)
        {
            const auto name = zip.getEntry (i)->filename;
            if (! name.startsWith ("META-INF") && (name.endsWithIgnoreCase (".xml") || name.endsWithIgnoreCase (".musicxml"))) idx = i;
        }
        if (idx < 0) return juce::Result::fail ("No MusicXML score found inside the .mxl file.");
        std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (idx));
        if (in == nullptr) return juce::Result::fail ("Could not unpack the .mxl file.");
        return readText (in->readEntireStreamAsString(), out);
    }
    return readText (mb.toString(), out);
}

ImportResult importFile (juce::ValueTree score, const juce::File& file, bool replace, juce::UndoManager* um)
{
    gp::Song song;
    auto r = read (file, song);
    if (r.failed())
    {
        ImportResult res;
        res.result = r;
        if (song.title.trim().isEmpty()) song.title = file.getFileNameWithoutExtension();
    return res;
    }
    if (song.title.trim().isEmpty()) song.title = file.getFileNameWithoutExtension();
    return gp::applySong (score, song, replace, um);
}

std::unique_ptr<juce::XmlElement> exportScore (const juce::ValueTree& score)
{
    auto root = std::make_unique<juce::XmlElement> ("score-partwise");
    root->setAttribute ("version", "4.0");
    add (*add (*root, "work"), "work-title", score[ids::title].toString());
    {
        auto* id = add (*root, "identification");
        if (score[ids::artist].toString().isNotEmpty())
            add (*id, "creator", score[ids::artist].toString())->setAttribute ("type", "composer");
        auto* enc = add (*id, "encoding");
        add (*enc, "software", juce::String ("FretForge Studio ") + FF_VERSION_STRING);
        add (*enc, "encoding-date", juce::Time::getCurrentTime().formatted ("%Y-%m-%d"));
    }
    auto* partList = add (*root, "part-list");
    const auto ms = measures (score);
    const auto starts = measureStarts (score);

    int ti = 0;
    for (auto track : tracks (score))
    {
        const auto pid = "P" + juce::String (++ti);
        auto* sp = add (*partList, "score-part");
        sp->setAttribute ("id", pid);
        add (*sp, "part-name", track[ids::name].toString());

        auto* part = add (*root, "part");
        part->setAttribute ("id", pid);
        const auto tuning = trackTuning (track);
        const int ns = (int) tuning.size();
        const int capo = (int) track[ids::capo];

        int prevNum = 0, prevDen = 0, prevKey = 99;
        double prevTempo = -1.0;
        std::vector<int> pendingLegato ((size_t) ns, -1), pendingSlide ((size_t) ns, -1);

        for (int m = 0; m < ms.getNumChildren(); ++m)
        {
            auto meas = ms.getChild (m);
            auto* xm = add (*part, "measure");
            xm->setAttribute ("number", m + 1);
            const int num = (int) meas.getProperty (ids::tsNum, 4), den = (int) meas.getProperty (ids::tsDen, 4);
            const int key = measureKey (score, m);
            if (m == 0 || num != prevNum || den != prevDen || key != prevKey)
            {
                auto* attr = add (*xm, "attributes");
                if (m == 0) add (*attr, "divisions", juce::String (divisionsPerQuarter));
                if (m == 0 || key != prevKey) add (*add (*attr, "key"), "fifths", juce::String (key));
                if (m == 0 || num != prevNum || den != prevDen)
                {
                    auto* time = add (*attr, "time");
                    add (*time, "beats", juce::String (num));
                    add (*time, "beat-type", juce::String (den));
                }
                if (m == 0)
                {
                    auto* clef = add (*attr, "clef");
                    add (*clef, "sign", "TAB");
                    add (*clef, "line", "5");
                    auto* sd = add (*attr, "staff-details");
                    add (*sd, "staff-lines", juce::String (ns));
                    for (int s = 0; s < ns; ++s)
                    {
                        const auto spt = spell (tuning[(size_t) s], 0);
                        auto* st = add (*sd, "staff-tuning");
                        st->setAttribute ("line", s + 1);
                        add (*st, "tuning-step", juce::String::charToString ((juce::juce_wchar) spt.step));
                        if (spt.alter != 0) add (*st, "tuning-alter", juce::String (spt.alter));
                        add (*st, "tuning-octave", juce::String (spt.octave));
                    }
                    if (capo > 0) add (*sd, "capo", juce::String (capo));
                }
                prevNum = num; prevDen = den; prevKey = key;
            }
            const double tempo = measureTempo (score, m);
            if (std::abs (tempo - prevTempo) > 1.0e-6)
            {
                auto* dir = add (*xm, "direction");
                dir->setAttribute ("placement", "above");
                auto* metro = add (*add (*dir, "direction-type"), "metronome");
                add (*metro, "beat-unit", "quarter");
                add (*metro, "per-minute", juce::String (juce::roundToInt (tempo)));
                add (*dir, "sound")->setAttribute ("tempo", tempo);
                prevTempo = tempo;
            }
            if (meas[ids::marker].toString().isNotEmpty())
            {
                auto* dir = add (*xm, "direction");
                dir->setAttribute ("placement", "above");
                add (*add (*dir, "direction-type"), "rehearsal", meas[ids::marker].toString());
            }

            const auto barNode = bar (track, m);
            double filled = 0.0;
            const double barLen = starts[(size_t) m + 1] - starts[(size_t) m];
            for (int bi = 0; bi < barNode.getNumChildren(); ++bi)
            {
                auto beat = barNode.getChild (bi);
                const int dur = (int) beat.getProperty (ids::dur, 4), dots = (int) beat[ids::dots], tuplet = (int) beat[ids::tuplet];
                filled += beatLengthQ (dur, dots, tuplet);
                addHarmony (*xm, beat[ids::chord].toString());

                std::vector<juce::ValueTree> notes;
                if (! (bool) beat[ids::rest])
                    for (auto n : beat)
                        if (juce::isPositiveAndBelow ((int) n[ids::string], ns)) notes.push_back (n);
                std::sort (notes.begin(), notes.end(), [] (auto& a, auto& b) { return (int) a[ids::string] < (int) b[ids::string]; });
                const size_t count = juce::jmax ((size_t) 1, notes.size());
                for (size_t k = 0; k < count; ++k)
                {
                    auto* xn = add (*xm, "note");
                    if (k > 0) add (*xn, "chord");
                    juce::ValueTree n = notes.empty() ? juce::ValueTree() : notes[k];
                    const int s = n.isValid() ? (int) n[ids::string] : 0;
                    const int fret = n.isValid() ? (int) n[ids::fret] : 0;
                    bool tieStart = false;
                    if (! n.isValid()) add (*xn, "rest");
                    else
                    {
                        int midi = tuning[(size_t) s] + fret + capo;
                        if ((int) n[ids::harmonic] == harmNatural) midi = tuning[(size_t) s] + capo + (int) naturalHarmonicSemis (fret);
                        addPitch (*xn, midi, key);
                        // a tie starts here when the next beat continues this string
                        auto next = bi + 1 < barNode.getNumChildren() ? barNode.getChild (bi + 1)
                                  : (m + 1 < ms.getNumChildren() ? bar (track, m + 1).getChild (0) : juce::ValueTree());
                        tieStart = next.isValid() && (bool) noteOnString (next, s)[ids::tie];
                    }
                    add (*xn, "duration", juce::String (ticks (dur, dots, tuplet)));
                    if (n.isValid() && (bool) n[ids::tie]) add (*xn, "tie")->setAttribute ("type", "stop");
                    if (tieStart) add (*xn, "tie")->setAttribute ("type", "start");
                    add (*xn, "voice", "1");
                    add (*xn, "type", typeName (dur));
                    for (int d = 0; d < dots; ++d) add (*xn, "dot");
                    if (tuplet > 0)
                    {
                        auto* tm = add (*xn, "time-modification");
                        add (*tm, "actual-notes", juce::String (tuplet));
                        add (*tm, "normal-notes", tuplet == 3 ? "2" : "4");
                    }
                    if (n.isValid() && (bool) n[ids::dead]) add (*xn, "notehead", "x");
                    else if (n.isValid() && (bool) n[ids::ghost]) add (*xn, "notehead", "normal")->setAttribute ("parentheses", "yes");

                    if (n.isValid())
                    {
                        auto* nots = add (*xn, "notations");
                        if ((bool) n[ids::tie]) add (*nots, "tied")->setAttribute ("type", "stop");
                        if (tieStart) add (*nots, "tied")->setAttribute ("type", "start");
                        if (pendingSlide[(size_t) s] >= 0) { add (*nots, "slide")->setAttribute ("type", "stop"); pendingSlide[(size_t) s] = -1; }
                        if ((int) n[ids::slide] == slideTo) { add (*nots, "slide")->setAttribute ("type", "start"); pendingSlide[(size_t) s] = fret; }
                        auto* tech = add (*nots, "technical");
                        if (pendingLegato[(size_t) s] >= 0)
                        {
                            const bool up = fret >= pendingLegato[(size_t) s];
                            add (*tech, up ? "hammer-on" : "pull-off")->setAttribute ("type", "stop");
                            pendingLegato[(size_t) s] = -1;
                        }
                        if ((bool) n[ids::hammer])
                        {
                            // direction is decided by the next note on the string
                            int nextFret = fret + 1;
                            auto next = bi + 1 < barNode.getNumChildren() ? barNode.getChild (bi + 1)
                                      : (m + 1 < ms.getNumChildren() ? bar (track, m + 1).getChild (0) : juce::ValueTree());
                            if (auto nn = noteOnString (next, s); nn.isValid()) nextFret = (int) nn[ids::fret];
                            const bool up = nextFret >= fret;
                            auto* h = add (*tech, up ? "hammer-on" : "pull-off", up ? "H" : "P");
                            h->setAttribute ("type", "start");
                            pendingLegato[(size_t) s] = fret;
                        }
                        if ((double) n[ids::bend] > 0.0)
                        {
                            auto* bend = add (*tech, "bend");
                            add (*bend, "bend-alter", juce::String ((double) n[ids::bend], 1));
                            if ((bool) n[ids::prebend]) add (*bend, "pre-bend");
                        }
                        if ((int) n[ids::harmonic] != harmNone)
                            add (*add (*tech, "harmonic"), (int) n[ids::harmonic] == harmNatural ? "natural" : "artificial");
                        if ((bool) n[ids::tap]) add (*tech, "tap");
                        if ((bool) n[ids::palmMute]) add (*tech, "other-technical", "P.M.");
                        if ((bool) n[ids::letRing]) add (*tech, "other-technical", "let ring");
                        add (*tech, "string", juce::String (ns - s));
                        add (*tech, "fret", juce::String (fret));
                        if ((bool) n[ids::accent] || (bool) n[ids::staccato])
                        {
                            auto* art = add (*nots, "articulations");
                            if ((bool) n[ids::accent]) add (*art, "accent");
                            if ((bool) n[ids::staccato]) add (*art, "staccato");
                        }
                        if ((bool) n[ids::vibrato]) add (*add (*nots, "ornaments"), "wavy-line")->setAttribute ("type", "start");
                    }
                    if (k == 0 && beat[ids::lyric].toString().isNotEmpty())
                    {
                        auto text = beat[ids::lyric].toString();
                        auto* lyr = add (*xn, "lyric");
                        lyr->setAttribute ("number", "1");
                        const bool cont = text.endsWithChar ('-');
                        add (*lyr, "syllabic", cont ? "begin" : "single");
                        add (*lyr, "text", cont ? text.dropLastCharacters (1) : text);
                    }
                }
            }
            // complete short bars with rests so the file stays valid
            double remaining = barLen - filled;
            while (remaining > 1.0e-6)
            {
                auto [dur, dots] = durationForQuarters (remaining);
                const double q = beatLengthQ (dur, dots, 0);
                if (q > remaining + 1.0e-6) { dur = 64; dots = 0; }
                auto* xn = add (*xm, "note");
                add (*xn, "rest");
                add (*xn, "duration", juce::String (ticks (dur, dots, 0)));
                add (*xn, "voice", "1");
                add (*xn, "type", typeName (dur));
                for (int d = 0; d < dots; ++d) add (*xn, "dot");
                remaining -= beatLengthQ (dur, dots, 0);
            }
        }
    }
    return root;
}

juce::Result exportFile (const juce::ValueTree& score, const juce::File& file)
{
    auto xml = exportScore (score);
    juce::XmlElement::TextFormat fmt;
    fmt.dtd = "<!DOCTYPE score-partwise PUBLIC \"-//Recordare//DTD MusicXML 4.0 Partwise//EN\" \"http://www.musicxml.org/dtds/partwise.dtd\">";
    if (! xml->writeTo (file, fmt))
        return juce::Result::fail ("Could not write " + file.getFullPathName());
    return juce::Result::ok();
}
} // namespace ff::tab::musicxml
