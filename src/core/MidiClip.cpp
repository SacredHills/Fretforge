#include "MidiClip.h"
#include "Timeline.h"
#include <algorithm>
#include <map>

namespace ff::midiclip
{
namespace tl = ff::timeline;

bool isMidiClip (const juce::ValueTree& clip)
{
    return clip.hasType (tl::ids::CLIP) && (bool) clip[midi];
}

std::vector<Note> notesOf (const juce::ValueTree& clip)
{
    std::vector<Note> out;
    const auto text = clip.getChildWithName (MIDI)[notes].toString();
    auto p = text.getCharPointer();
    while (! p.isEmpty())
    {
        Note n;
        n.start = juce::CharacterFunctions::readDoubleValue (p);
        if (*p == ',') ++p;
        n.length = juce::CharacterFunctions::readDoubleValue (p);
        if (*p == ',') ++p;
        n.pitch = (int) juce::CharacterFunctions::readDoubleValue (p);
        if (*p == ',') ++p;
        n.velocity = (int) juce::CharacterFunctions::readDoubleValue (p);
        while (! p.isEmpty() && *p != ';') ++p;
        if (*p == ';') ++p;
        if (n.length > 0.0 && n.pitch >= 0 && n.pitch < 128) out.push_back (n);
    }
    return out;
}

std::vector<Pedal> pedalOf (const juce::ValueTree& clip)
{
    std::vector<Pedal> out;
    const auto text = clip.getChildWithName (MIDI)[sustain].toString();
    auto p = text.getCharPointer();
    while (! p.isEmpty())
    {
        Pedal e;
        e.time = juce::CharacterFunctions::readDoubleValue (p);
        if (*p == ',') ++p;
        e.down = juce::CharacterFunctions::readDoubleValue (p) >= 0.5;
        while (! p.isEmpty() && *p != ';') ++p;
        if (*p == ';') ++p;
        out.push_back (e);
    }
    return out;
}

void setNotes (juce::ValueTree clip, const std::vector<Note>& ns, const std::vector<Pedal>& ped, juce::UndoManager* um)
{
    juce::String t, s;
    t.preallocateBytes ((size_t) ns.size() * 24);
    for (auto& n : ns)
        t << juce::String (n.start, 5) << ',' << juce::String (n.length, 5) << ',' << n.pitch << ',' << n.velocity << ';';
    for (auto& e : ped)
        s << juce::String (e.time, 5) << ',' << (e.down ? 1 : 0) << ';';
    auto m = clip.getChildWithName (MIDI);
    if (! m.isValid())
    {
        m = juce::ValueTree (MIDI);
        clip.appendChild (m, um);
    }
    m.setProperty (notes, t, um);
    m.setProperty (sustain, s, um);
}

juce::ValueTree createMidiClip (const juce::String& name, double start, const std::vector<Note>& ns, const std::vector<Pedal>& ped)
{
    double end = 0.0;
    for (auto& n : ns) end = juce::jmax (end, n.start + n.length);
    auto c = tl::createClip ({}, name, start, juce::jmax (0.25, end), 0.0);
    c.setProperty (midi, true, nullptr);
    setNotes (c, ns, ped, nullptr);
    return c;
}

bool pitchRange (const std::vector<Note>& ns, int& lo, int& hi)
{
    if (ns.empty()) return false;
    lo = 127; hi = 0;
    for (auto& n : ns) { lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); }
    return true;
}

ImportedFile readMidiFile (const juce::File& f)
{
    ImportedFile r;
    juce::FileInputStream in (f);
    juce::MidiFile mf;
    if (! in.openedOk() || ! mf.readFrom (in))
    {
        r.error = "\"" + f.getFileName() + "\" is not a MIDI file FretForge can read.";
        return r;
    }
    juce::MidiMessageSequence tempos;
    mf.findAllTempoEvents (tempos);
    for (auto* e : tempos)
        if (e->message.isTempoMetaEvent()) { r.firstTempo = 60.0 / e->message.getTempoSecondsPerQuarterNote(); break; }
    juce::MidiMessageSequence sigs;
    mf.findAllTimeSigEvents (sigs);
    for (auto* e : sigs)
        if (e->message.isTimeSignatureMetaEvent()) { e->message.getTimeSignatureInfo (r.tsNum, r.tsDen); break; }
    mf.convertTimestampTicksToSeconds();

    for (int ti = 0; ti < mf.getNumTracks(); ++ti)
    {
        const auto* seq = mf.getTrack (ti);
        juce::String trackName;
        // per channel: pending note-ons, notes, pedal, program
        struct Chan { std::vector<Note> notes; std::vector<Pedal> pedal; int program = -1; std::map<int, std::pair<double, int>> on; };
        std::map<int, Chan> chans;
        for (auto* e : *seq)
        {
            const auto& m = e->message;
            const double t = m.getTimeStamp();
            if (m.isTrackNameEvent() && trackName.isEmpty()) { trackName = m.getTextFromTextMetaEvent().trim(); continue; }
            const int ch = m.getChannel();
            if (ch <= 0) continue;
            auto& c = chans[ch];
            if (m.isNoteOn())
            {
                if (auto it = c.on.find (m.getNoteNumber()); it != c.on.end())   // retrigger: end the previous one
                    c.notes.push_back ({ it->second.first, juce::jmax (0.005, t - it->second.first), m.getNoteNumber(), it->second.second });
                c.on[m.getNoteNumber()] = { t, m.getVelocity() };
            }
            else if (m.isNoteOff())
            {
                if (auto it = c.on.find (m.getNoteNumber()); it != c.on.end())
                {
                    c.notes.push_back ({ it->second.first, juce::jmax (0.005, t - it->second.first), m.getNoteNumber(), it->second.second });
                    c.on.erase (it);
                }
            }
            else if (m.isSustainPedalOn()) c.pedal.push_back ({ t, true });
            else if (m.isSustainPedalOff()) c.pedal.push_back ({ t, false });
            else if (m.isProgramChange() && c.program < 0) c.program = m.getProgramChangeNumber();
        }
        const double end = seq->getEndTime();
        for (auto& [ch, c] : chans)
        {
            for (auto& [pitch, st] : c.on) c.notes.push_back ({ st.first, juce::jmax (0.05, end - st.first), pitch, st.second });
            if (c.notes.empty()) continue;
            std::sort (c.notes.begin(), c.notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
            ImportedTrack it;
            it.channel = ch;
            it.program = c.program;
            it.notes = std::move (c.notes);
            it.pedal = std::move (c.pedal);
            it.name = trackName.isNotEmpty() ? trackName
                    : ch == 10 ? juce::String ("Drums")
                    : c.program >= 0 ? gmProgramName (c.program) : "MIDI " + juce::String (r.tracks.size() + 1);
            if (chans.size() > 1 && trackName.isNotEmpty()) it.name << " (ch " << ch << ")";
            r.tracks.push_back (std::move (it));
        }
    }
    if (r.tracks.empty()) r.error = "\"" + f.getFileName() + "\" has no notes.";
    return r;
}

juce::String gmProgramName (int program)
{
    return juce::MidiMessage::getGMInstrumentName (juce::jlimit (0, 127, program));
}
} // namespace ff::midiclip
