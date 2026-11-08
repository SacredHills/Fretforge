#include "PianoRoll.h"
#include "core/Timeline.h"
#include "core/ProjectFile.h"
#include <cmath>

namespace ff
{
namespace tl = timeline;

namespace
{
const char* gridNames[] = { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/8 triplet", "1/16 triplet", "Off" };
const double gridQuarters[] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0, 0.0 };
const char* lengthNames[] = { "Grid", "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" };
const double lengthQuarters[] = { 0.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125 };
bool isBlackKey (int pitch) { const int k = pitch % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }
}

PianoRoll::PianoRoll (ProjectDocument& d, EngineHost& h, juce::ValueTree c) : doc (d), host (h), clip (c)
{
    setWantsKeyboardFocus (true);
    for (int i = 0; i < (int) std::size (gridNames); ++i) gridBox.addItem (gridNames[i], i + 1);
    gridBox.setSelectedId (5, juce::dontSendNotification);   // 1/16
    gridBox.onChange = [this] { flashTicks = 0; updateInfo(); repaint(); };
    for (int i = 0; i < (int) std::size (lengthNames); ++i) lengthBox.addItem (lengthNames[i], i + 1);
    lengthBox.setSelectedId (1, juce::dontSendNotification);
    gridLabel.setText ("Grid", juce::dontSendNotification);
    lengthLabel.setText ("Length", juce::dontSendNotification);
    lengthBox.setTooltip ("Length of the notes you add (Draw tool or double-click). Grid: one grid step.");
    gridBox.setTooltip ("Grid: where notes snap when you add, move or resize them (hold Alt to place freely), and what Quantize moves notes onto");
    for (auto* l : { &gridLabel, &lengthLabel, &info })
    {
        l->setColour (juce::Label::textColourId, palette::textDim);
        l->setFont (uiFont (12.0f));
        addAndMakeVisible (*l);
    }
    drawButton.setClickingTogglesState (true);
    drawButton.setColour (juce::TextButton::buttonOnColourId, palette::accent);
    drawButton.setTooltip ("Draw: click to add notes and drag to set their length (D). Off: drag to select, double-click to add.");
    quantizeButton.setTooltip ("Move the selected notes (or all of them) onto the grid (Q)");
    quantizeButton.onClick = [this] { quantize(); };
    zoomIn.onClick = [this] { pps = juce::jmin (4000.0, pps * 1.4); repaint(); };
    zoomOut.onClick = [this] { pps = juce::jmax (10.0, pps / 1.4); repaint(); };
    fitButton.onClick = [this] { fit(); };
    for (auto* c2 : std::initializer_list<juce::Component*> { &drawButton, &quantizeButton, &zoomIn, &zoomOut, &fitButton, &gridBox, &lengthBox })
    {
        c2->setWantsKeyboardFocus (false);
        addAndMakeVisible (c2);
    }
    for (auto* sb : { &hbar, &vbar })
    {
        sb->setAutoHide (false);
        sb->addListener (this);
        sb->setWantsKeyboardFocus (false);
        addAndMakeVisible (sb);
    }
    clip.addListener (this);
    load();
    startTimerHz (30);
}

PianoRoll::~PianoRoll()
{
    stopAudition();
    clip.removeListener (this);
}

// ------------------------------------------------------------------ geometry

juce::Rectangle<int> PianoRoll::gridArea() const
{
    return getLocalBounds().withTrimmedTop (toolbarH + rulerH).withTrimmedLeft (keysW).withTrimmedBottom (velH + sbW).withTrimmedRight (sbW);
}
juce::Rectangle<int> PianoRoll::velArea() const
{
    return getLocalBounds().withTrimmedBottom (sbW).removeFromBottom (velH).withTrimmedLeft (keysW).withTrimmedRight (sbW).withTrimmedTop (4);
}
juce::Rectangle<int> PianoRoll::keysArea() const
{
    return getLocalBounds().withTrimmedTop (toolbarH + rulerH).withTrimmedBottom (velH + sbW).removeFromLeft (keysW);
}
juce::Rectangle<int> PianoRoll::rulerArea() const
{
    return { keysW, toolbarH, getWidth() - keysW - sbW, rulerH };
}

void PianoRoll::updateScrollBars()
{
    const auto g = gridArea();
    if (g.getWidth() <= 0 || g.getHeight() <= 0) return;
    const double view = g.getWidth() / pps;
    double end = clipEnd();
    for (auto& n : notes) end = juce::jmax (end, absStart (n) + n.length);
    end += 4.0 * quarterSec() * juce::jmax (1, doc.tsNum());
    const double lo = juce::jmin (0.0, scrollT), hi = juce::jmax (end, scrollT + view);
    hbar.setRangeLimits (lo, hi, juce::dontSendNotification);
    hbar.setCurrentRange (scrollT, view, juce::dontSendNotification);
    const double rows = g.getHeight() / (double) keyH;
    vbar.setRangeLimits (0.0, 128.0, juce::dontSendNotification);
    vbar.setCurrentRange (127.0 - topPitch, rows, juce::dontSendNotification);
}

void PianoRoll::scrollBarMoved (juce::ScrollBar* sb, double start)
{
    if (sb == &hbar) scrollT = start;
    else topPitch = (float) (127.0 - start);
    repaint();
}

void PianoRoll::flash (const juce::String& message)
{
    info.setText (message, juce::dontSendNotification);
    info.setColour (juce::Label::textColourId, palette::accent);
    flashTicks = 75;   // 2.5 s at 30 Hz
}
float PianoRoll::xFor (double t) const { return (float) gridArea().getX() + (float) ((t - scrollT) * pps); }
double PianoRoll::timeFor (float x) const { return scrollT + (x - (float) gridArea().getX()) / pps; }
float PianoRoll::yFor (int pitch) const { return (float) gridArea().getY() + (topPitch - (float) pitch) * keyH; }
int PianoRoll::pitchFor (float y) const { return juce::jlimit (0, 127, (int) std::floor (topPitch - (y - (float) gridArea().getY()) / keyH) + 1); }
double PianoRoll::clipStart() const { return (double) clip[tl::ids::start]; }
double PianoRoll::clipEnd() const { return clipStart() + (double) clip[tl::ids::length]; }
double PianoRoll::offset() const { return (double) clip[tl::ids::offset]; }
double PianoRoll::quarterSec() const { return 60.0 / juce::jmax (20.0, doc.tempo()); }
double PianoRoll::gridSec() const { return gridQuarters[juce::jlimit (0, (int) std::size (gridQuarters) - 1, gridBox.getSelectedId() - 1)] * quarterSec(); }
double PianoRoll::snap (double t, bool on) const
{
    const double g = gridSec();
    if (! on || g <= 0.0) return t;
    return std::round (t / g) * g;
}
double PianoRoll::newNoteLength() const
{
    const double q = lengthQuarters[juce::jlimit (0, (int) std::size (lengthQuarters) - 1, lengthBox.getSelectedId() - 1)];
    const double g = q > 0.0 ? q * quarterSec() : gridSec();
    return g > 0.0 ? g : quarterSec() * 0.25;
}

juce::Rectangle<float> PianoRoll::noteRect (const midiclip::Note& n) const
{
    const float x0 = xFor (absStart (n)), x1 = xFor (absStart (n) + n.length);
    return { x0, yFor (n.pitch) + 1.0f, juce::jmax (3.0f, x1 - x0 - 1.0f), keyH - 2.0f };
}

int PianoRoll::noteAt (juce::Point<float> p, bool& edge) const
{
    for (int i = (int) notes.size(); --i >= 0;)
    {
        const auto r = noteRect (notes[(size_t) i]);
        if (r.expanded (0.0f, 1.0f).contains (p))
        {
            edge = p.x > r.getRight() - juce::jmin (7.0f, r.getWidth() * 0.35f);
            return i;
        }
    }
    edge = false;
    return -1;
}

int PianoRoll::trackIndex() const { return doc.tracks().indexOf (clip.getParent()); }
int PianoRoll::numSelected() const { int n = 0; for (auto s : selected) n += s ? 1 : 0; return n; }

// ------------------------------------------------------------------ data

void PianoRoll::load()
{
    notes = midiclip::notesOf (clip);
    pedal = midiclip::pedalOf (clip);
    selected.assign (notes.size(), 0);
    updateInfo();
    repaint();
}

void PianoRoll::beginEdit (const juce::String& name)
{
    doc.undo().beginNewTransaction (name);
    changed = false;
}

void PianoRoll::commit (const juce::String& undoName)
{
    if (undoName.isNotEmpty()) doc.undo().beginNewTransaction (undoName);
    const juce::ScopedValueSetter<bool> svs (writing, true);
    midiclip::setNotes (clip, notes, pedal, &doc.undo());
    // grow the clip to hold notes past its end
    double end = clipEnd();
    for (auto& n : notes) end = juce::jmax (end, absStart (n) + n.length);
    if (end > clipEnd() + 1.0e-6) clip.setProperty (tl::ids::length, end - clipStart(), &doc.undo());
    changed = true;
    updateInfo();
    repaint();
}

void PianoRoll::updateInfo()
{
    if (flashTicks > 0) return;
    info.setColour (juce::Label::textColourId, palette::textDim);
    const int s = numSelected();
    juce::String t = juce::String ((int) notes.size()) + " notes";
    if (s > 0)
    {
        t << ", " << s << " selected";
        if (s == 1)
            for (size_t i = 0; i < notes.size(); ++i)
                if (selected[i])
                    t << ":  " << juce::MidiMessage::getMidiNoteName (notes[i].pitch, true, true, 4) << "  velocity " << notes[i].velocity;
    }
    info.setText (t, juce::dontSendNotification);
}

void PianoRoll::audition (int pitch, int velocity)
{
    stopAudition();
    keyboardNote = pitch;
    host.engine.pushLiveMidi (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) juce::jlimit (1, 127, velocity)), trackIndex());
}

void PianoRoll::stopAudition()
{
    if (keyboardNote >= 0) host.engine.pushLiveMidi (juce::MidiMessage::noteOff (1, keyboardNote), trackIndex());
    keyboardNote = -1;
}

// ------------------------------------------------------------------ editing

void PianoRoll::quantize()
{
    if (notes.empty()) { flash ("No notes to quantize"); return; }
    if (gridSec() <= 0.0) { flash ("Grid is Off: pick a grid (e.g. 1/16) to quantize to"); return; }
    const bool any = numSelected() > 0;
    int moved = 0, count = 0;
    for (size_t i = 0; i < notes.size(); ++i)
        if (! any || selected[i])
        {
            auto& n = notes[i];
            ++count;
            const double a = snap (absStart (n), true);
            const double s = a - clipStart() + offset();
            if (std::abs (s - n.start) > 1.0e-5) { n.start = s; ++moved; }
        }
    const auto gridName = gridBox.getText();
    if (moved == 0)
    {
        flash (juce::String (any ? "The selected notes are" : "All notes are") + " already on the " + gridName + " grid");
        return;
    }
    commit ("Quantize notes");
    flash ("Quantized " + juce::String (moved) + " of " + juce::String (count) + " notes to " + gridName);
}

namespace
{
    std::vector<midiclip::Note> noteClipboard;   // shared by all MIDI editors; starts relative to the first note
}

void PianoRoll::copySelected()
{
    if (numSelected() == 0) { flash ("Select notes to copy"); return; }
    double first = 1.0e12;
    for (size_t i = 0; i < notes.size(); ++i) if (selected[i]) first = juce::jmin (first, absStart (notes[i]));
    noteClipboard.clear();
    for (size_t i = 0; i < notes.size(); ++i)
        if (selected[i])
        {
            auto n = notes[i];
            n.start = absStart (n) - first;
            noteClipboard.push_back (n);
        }
    flash ("Copied " + juce::String ((int) noteClipboard.size()) + (noteClipboard.size() == 1 ? " note" : " notes"));
}

void PianoRoll::pasteAt (double t)
{
    if (noteClipboard.empty()) { flash ("Nothing to paste: copy some notes first (Ctrl+C)"); return; }
    const double base = juce::jmax (clipStart(), t);
    selected.assign (notes.size(), 0);
    for (auto n : noteClipboard)
    {
        n.start = base + n.start - clipStart() + offset();
        notes.push_back (n);
        selected.push_back (1);
    }
    commit ("Paste notes");
    flash ("Pasted " + juce::String ((int) noteClipboard.size()) + (noteClipboard.size() == 1 ? " note" : " notes"));
}

void PianoRoll::duplicateSelected()
{
    if (numSelected() == 0) { flash ("Select notes to duplicate"); return; }
    const auto saved = noteClipboard;
    double first = 1.0e12, last = 0.0;
    for (size_t i = 0; i < notes.size(); ++i)
        if (selected[i]) { first = juce::jmin (first, absStart (notes[i])); last = juce::jmax (last, absStart (notes[i]) + notes[i].length); }
    copySelected();
    const double gs = gridSec();
    const double span = gs > 0.0 ? std::ceil ((last - first) / gs - 1.0e-6) * gs : last - first;
    pasteAt (first + juce::jmax (span, gs > 0.0 ? gs : 0.01));
    noteClipboard = saved;   // duplicating leaves the clipboard alone
    flashTicks = 0;
    updateInfo();
}

void PianoRoll::deleteSelected()
{
    if (numSelected() == 0) return;
    std::vector<midiclip::Note> keep;
    for (size_t i = 0; i < notes.size(); ++i) if (! selected[i]) keep.push_back (notes[i]);
    notes = std::move (keep);
    selected.assign (notes.size(), 0);
    commit ("Delete notes");
}

void PianoRoll::transpose (int semis)
{
    if (numSelected() == 0) return;
    for (size_t i = 0; i < notes.size(); ++i) if (selected[i]) notes[i].pitch = juce::jlimit (0, 127, notes[i].pitch + semis);
    commit ("Transpose notes");
}

void PianoRoll::nudge (int steps)
{
    const double g = gridSec() > 0.0 ? gridSec() : quarterSec() * 0.25;
    if (numSelected() == 0) return;
    for (size_t i = 0; i < notes.size(); ++i)
        if (selected[i]) notes[i].start = juce::jmax (offset() - clipStart() + 0.0, notes[i].start + steps * g);
    commit ("Move notes");
}

void PianoRoll::fit()
{
    const auto g = gridArea();
    double a = clipStart(), b = clipEnd();
    int lo = 60, hi = 72;
    if (midiclip::pitchRange (notes, lo, hi)) {}
    pps = juce::jlimit (10.0, 4000.0, (g.getWidth() - 20.0) / juce::jmax (0.25, b - a));
    scrollT = a - 10.0 / pps;
    const int rows = juce::jmax (12, hi - lo + 5);
    keyH = juce::jlimit (6.0f, 24.0f, (float) g.getHeight() / (float) rows);
    topPitch = (float) juce::jmin (127, hi + 2 + juce::jmax (0, ((int) (g.getHeight() / keyH) - rows) / 2));
    repaint();
}

// ------------------------------------------------------------------ painting

void PianoRoll::paint (juce::Graphics& g)
{
    updateScrollBars();
    g.fillAll (palette::bg);
    const auto grid = gridArea();
    const auto keys = keysArea();
    const auto vel = velArea();

    // toolbar background
    g.setColour (palette::panel);
    g.fillRect (getLocalBounds().removeFromTop (toolbarH));

    // grid rows
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (grid);
        for (int p = 127; p >= 0; --p)
        {
            const float y = yFor (p);
            if (y > (float) grid.getBottom() || y + keyH < (float) grid.getY()) continue;
            g.setColour (isBlackKey (p) ? palette::bg.darker (0.45f) : palette::bg.brighter (0.07f));
            g.fillRect ((float) grid.getX(), y, (float) grid.getWidth(), keyH);
            // a line under every row; stronger between E / F and at each octave (under C)
            const int k = p % 12;
            g.setColour (k == 0 ? juce::Colours::white.withAlpha (0.22f) : k == 5 ? juce::Colours::white.withAlpha (0.12f) : juce::Colours::black.withAlpha (0.5f));
            g.drawHorizontalLine ((int) (y + keyH), (float) grid.getX(), (float) grid.getRight());
        }
        // outside the clip: darker
        const float cx0 = xFor (clipStart()), cx1 = xFor (clipEnd());
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> ((float) grid.getX(), (float) grid.getY(), cx0 - (float) grid.getX(), (float) grid.getHeight()));
        g.fillRect (juce::Rectangle<float> (cx1, (float) grid.getY(), (float) grid.getRight() - cx1, (float) grid.getHeight()));

        // grid: a line at every grid step (bars strongest, then beats, then the rest); alternate bars shaded
        const double q = quarterSec();
        const int num = juce::jmax (1, doc.tsNum()), den = juce::jmax (1, doc.tsDen());
        const double beat = q * 4.0 / den, bar = beat * num;
        const double t0 = timeFor ((float) grid.getX()), t1 = timeFor ((float) grid.getRight());
        for (auto b = (juce::int64) std::floor (t0 / bar); (double) b * bar < t1; ++b)
            if ((b & 1) != 0)
            {
                g.setColour (juce::Colours::white.withAlpha (0.025f));
                g.fillRect (juce::Rectangle<float> (xFor ((double) b * bar), (float) grid.getY(), (float) (bar * pps), (float) grid.getHeight()));
            }
        double step = gridSec() > 0.0 ? gridSec() : beat;
        while (step * pps < 6.0 && step < bar) step = step < beat ? beat : bar;   // too dense to draw: coarser lines
        const auto onMultiple = [] (double t, double m) { return std::abs (std::remainder (t, m)) < 1.0e-6; };
        for (auto k = (juce::int64) std::floor (t0 / step); (double) k * step < t1; ++k)
        {
            const double t = (double) k * step;
            const bool isBar = onMultiple (t, bar), isBeat = onMultiple (t, beat);
            g.setColour (juce::Colours::white.withAlpha (isBar ? 0.42f : isBeat ? 0.2f : 0.08f));
            g.drawVerticalLine ((int) xFor (t), (float) grid.getY(), (float) grid.getBottom());
        }

        // notes
        const auto colour = palette::graph;
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const auto& n = notes[i];
            const auto r = noteRect (n);
            if (r.getRight() < (float) grid.getX() || r.getX() > (float) grid.getRight()) continue;
            const bool inside = absStart (n) < clipEnd() && absStart (n) >= clipStart() - 1.0e-9;
            auto c = colour.withMultipliedBrightness (0.55f + 0.45f * (float) n.velocity / 127.0f);
            if (! inside) c = c.withAlpha (0.35f);
            g.setColour (selected[i] ? juce::Colours::white : c);
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (selected[i] ? palette::text : c.darker (0.5f));
            g.drawRoundedRectangle (r, 2.0f, 1.0f);
        }
        if (! marquee.isEmpty())
        {
            g.setColour (palette::info.withAlpha (0.15f));
            g.fillRect (marquee);
            g.setColour (palette::info);
            g.drawRect (marquee, 1.0f);
        }
        // playhead
        const float px = xFor (host.engine.getPositionSeconds());
        g.setColour (palette::accent);
        g.drawVerticalLine ((int) px, (float) grid.getY(), (float) grid.getBottom());
    }

    // ruler
    {
        auto ruler = juce::Rectangle<int> (grid.getX(), toolbarH, grid.getWidth(), rulerH);
        g.setColour (palette::well);
        g.fillRect (ruler);
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (ruler);
        const double q = quarterSec();
        const double bar = q * 4.0 / juce::jmax (1, doc.tsDen()) * juce::jmax (1, doc.tsNum());
        g.setFont (uiFont (10.5f));
        const double t0 = timeFor ((float) grid.getX()), t1 = timeFor ((float) grid.getRight());
        const int step = bar * pps < 40.0 ? (bar * pps < 12.0 ? 8 : 4) : 1;
        for (int b = juce::jmax (0, (int) std::floor (t0 / bar)); b * bar < t1; ++b)
        {
            if (b % step != 0) continue;
            const float x = xFor (b * bar);
            g.setColour (palette::textDim);
            g.drawVerticalLine ((int) x, (float) ruler.getY() + 8.0f, (float) ruler.getBottom());
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (x + 3.0f, (float) ruler.getY(), 40.0f, (float) rulerH), juce::Justification::centredLeft);
        }
        g.setColour (palette::accent.withAlpha (0.5f));
        g.fillRect (juce::Rectangle<float> (xFor (clipStart()), (float) ruler.getBottom() - 3.0f, xFor (clipEnd()) - xFor (clipStart()), 3.0f));
        // playhead marker (click or drag in the ruler to move it)
        const float px = xFor (host.engine.getPositionSeconds());
        juce::Path tri;
        tri.addTriangle (px - 6.0f, (float) ruler.getY() + 6.0f, px + 6.0f, (float) ruler.getY() + 6.0f, px, (float) ruler.getBottom());
        g.setColour (palette::accent);
        g.fillPath (tri);
    }

    // keyboard
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (keys);
        // white keys first (each spans its row plus half of a neighbouring black key's), then the black keys over them
        g.setColour (juce::Colour (0xff15171a));
        g.fillRect (keys);
        for (int p = 127; p >= 0; --p)
        {
            if (isBlackKey (p)) continue;
            float top = yFor (p), bottom = top + keyH;
            if (p < 127 && isBlackKey (p + 1)) top -= keyH * 0.5f;
            if (p > 0 && isBlackKey (p - 1)) bottom += keyH * 0.5f;
            if (top > (float) keys.getBottom() || bottom < (float) keys.getY()) continue;
            auto r = juce::Rectangle<float> ((float) keys.getX(), std::round (top), (float) keys.getWidth() - 1.0f, std::round (bottom) - std::round (top) - 1.0f);
            g.setColour (p == keyboardNote ? palette::graph : juce::Colour (0xffe4e7eb));
            g.fillRect (r);
            if (p % 12 == 0 && keyH >= 7.0f)
            {
                g.setColour (juce::Colour (0xff30343a));
                g.setFont (uiFont (juce::jmin (11.0f, keyH), true));
                g.drawText (juce::MidiMessage::getMidiNoteName (p, true, true, 4), r.reduced (3.0f, 0.0f).withTrimmedTop (r.getHeight() - keyH), juce::Justification::centredRight);
            }
        }
        for (int p = 127; p >= 0; --p)
        {
            if (! isBlackKey (p)) continue;
            const float y = yFor (p);
            if (y > (float) keys.getBottom() || y + keyH < (float) keys.getY()) continue;
            const auto r = juce::Rectangle<float> ((float) keys.getX(), y + 0.5f, (float) keys.getWidth() * 0.62f, keyH - 1.0f);
            g.setColour (p == keyboardNote ? palette::graph : juce::Colour (0xff1d2024));
            g.fillRoundedRectangle (r, 1.5f);
        }
    }

    // velocity lane
    g.setColour (palette::well);
    g.fillRect (vel);
    g.setColour (palette::outline);
    g.drawHorizontalLine (vel.getY(), (float) vel.getX(), (float) vel.getRight());
    g.setColour (palette::textDim);
    g.setFont (uiFont (10.0f));
    g.drawText ("VEL", juce::Rectangle<int> (0, vel.getY(), keysW, 16), juce::Justification::centred);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (vel);
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const float x = xFor (absStart (notes[i]));
            if (x < (float) vel.getX() - 4.0f || x > (float) vel.getRight()) continue;
            const float h = (float) (vel.getHeight() - 6) * (float) notes[i].velocity / 127.0f;
            g.setColour (selected[i] ? juce::Colours::white : palette::graph.withAlpha (0.85f));
            g.fillRect (x, (float) vel.getBottom() - h, 3.0f, h);
            g.fillEllipse (x - 2.0f, (float) vel.getBottom() - h - 3.0f, 7.0f, 7.0f);
        }
    }

    // velocity value by the cursor while dragging
    if (velTip >= 0)
    {
        const juce::String t (velTip);
        g.setFont (uiFont (12.0f, true));
        auto r = juce::Rectangle<float> (velTipPos.x + 12.0f, velTipPos.y - 26.0f, 34.0f, 20.0f);
        r = r.constrainedWithin (getLocalBounds().toFloat().reduced (2.0f));
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (palette::accent);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        g.setColour (juce::Colours::white);
        g.drawText (t, r, juce::Justification::centred);
    }
}

void PianoRoll::resized()
{
    auto tb = getLocalBounds().removeFromTop (toolbarH).reduced (8, 5);
    drawButton.setBounds (tb.removeFromLeft (62)); tb.removeFromLeft (10);
    gridLabel.setBounds (tb.removeFromLeft (34));
    gridBox.setBounds (tb.removeFromLeft (110)); tb.removeFromLeft (10);
    lengthLabel.setBounds (tb.removeFromLeft (70));
    lengthBox.setBounds (tb.removeFromLeft (80)); tb.removeFromLeft (10);
    quantizeButton.setBounds (tb.removeFromLeft (80)); tb.removeFromLeft (10);
    fitButton.setBounds (tb.removeFromRight (44)); tb.removeFromRight (4);
    zoomIn.setBounds (tb.removeFromRight (30)); tb.removeFromRight (2);
    zoomOut.setBounds (tb.removeFromRight (30)); tb.removeFromRight (10);
    info.setBounds (tb);
    const auto grid = gridArea();
    hbar.setBounds (keysW, getHeight() - sbW, getWidth() - keysW - sbW, sbW);
    vbar.setBounds (grid.getRight(), grid.getY(), sbW, grid.getHeight());
}

// ------------------------------------------------------------------ mouse

void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    downPos = e.position;
    drag = Drag::none;
    changed = false;
    const bool snapOn = ! e.mods.isAltDown();
    flashTicks = 0;

    if (rulerArea().contains (e.getPosition()))   // set the play position
    {
        drag = Drag::scrub;
        host.engine.locateSeconds (snap (timeFor (e.position.x), snapOn));
        repaint();
        return;
    }
    if (keysArea().contains (e.getPosition()))
    {
        drag = Drag::keyboard;
        audition (pitchFor (e.position.y), 100);
        repaint();
        return;
    }
    if (velArea().contains (e.getPosition()))
    {
        // the nearest velocity bar
        int best = -1;
        float bestD = 8.0f;
        for (size_t i = 0; i < notes.size(); ++i)
            if (const float d = std::abs (xFor (absStart (notes[i])) - e.position.x); d < bestD) { bestD = d; best = (int) i; }
        if (best < 0) return;
        anchor = best;
        if (! selected[(size_t) best]) { std::fill (selected.begin(), selected.end(), 0); selected[(size_t) best] = 1; }
        beginEdit ("Note velocity");
        origs.clear();
        for (auto& n : notes) origs.push_back ({ n.start, n.length, n.pitch, n.velocity });
        drag = Drag::velocity;
        mouseDrag (e);
        return;
    }
    if (! gridArea().contains (e.getPosition())) return;

    bool edge = false;
    const int hit = noteAt (e.position, edge);
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        if (hit >= 0 && ! selected[(size_t) hit]) { selected.assign (notes.size(), 0); selected[(size_t) hit] = 1; updateInfo(); repaint(); }
        const bool any = numSelected() > 0;
        m.addItem (4, "Cut  (Ctrl+X)", any);
        m.addItem (5, "Copy  (Ctrl+C)", any);
        m.addItem (6, "Paste here  (Ctrl+V pastes at the playhead)", ! noteClipboard.empty());
        m.addItem (7, "Duplicate  (Ctrl+D)", any);
        m.addItem (1, "Delete", any);
        m.addSeparator();
        m.addItem (2, "Quantize");
        m.addItem (3, "Select all");
        juce::Component::SafePointer<PianoRoll> safe (this);
        const double at = snap (timeFor (e.position.x), ! e.mods.isAltDown());
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, at] (int r) {
            if (safe == nullptr) return;
            if (r == 1) safe->deleteSelected();
            else if (r == 4) { safe->copySelected(); safe->deleteSelected(); }
            else if (r == 5) safe->copySelected();
            else if (r == 6) safe->pasteAt (at);
            else if (r == 7) safe->duplicateSelected();
            else if (r == 2) safe->quantize();
            else if (r == 3) { std::fill (safe->selected.begin(), safe->selected.end(), 1); safe->updateInfo(); safe->repaint(); }
        });
        return;
    }
    if (hit >= 0)
    {
        if (e.mods.isShiftDown() || e.mods.isCommandDown()) selected[(size_t) hit] = ! selected[(size_t) hit];
        else if (! selected[(size_t) hit]) { std::fill (selected.begin(), selected.end(), 0); selected[(size_t) hit] = 1; }
        anchor = hit;
        origs.clear();
        for (auto& n : notes) origs.push_back ({ n.start, n.length, n.pitch, n.velocity });
        drag = edge ? Drag::resize : Drag::move;
        beginEdit (edge ? "Note length" : "Move notes");
        if (! edge) audition (notes[(size_t) hit].pitch, notes[(size_t) hit].velocity);
        updateInfo();
        repaint();
        return;
    }
    if (drawButton.getToggleState())
    {
        // draw a note: click to add it, drag to set its length
        midiclip::Note n;
        const double t = snap (timeFor (e.position.x), snapOn);
        n.start = t - clipStart() + offset();
        n.length = newNoteLength();
        n.pitch = pitchFor (e.position.y);
        n.velocity = 100;
        notes.push_back (n);
        selected.assign (notes.size(), 0);
        selected.back() = 1;
        anchor = (int) notes.size() - 1;
        origs.clear();
        for (auto& x : notes) origs.push_back ({ x.start, x.length, x.pitch, x.velocity });
        beginEdit ("Add note");
        commit();
        audition (n.pitch, n.velocity);
        drag = Drag::draw;
        return;
    }
    if (! e.mods.isShiftDown()) std::fill (selected.begin(), selected.end(), 0);
    drag = Drag::marquee;
    marquee = {};
    updateInfo();
    repaint();
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    const bool snapOn = ! e.mods.isAltDown();
    switch (drag)
    {
        case Drag::scrub:
            host.engine.locateSeconds (snap (timeFor (e.position.x), snapOn));
            repaint();
            break;
        case Drag::keyboard:
        {
            const int p = pitchFor (e.position.y);
            if (p != keyboardNote) audition (p, 100);
            repaint();
            break;
        }
        case Drag::move:
        {
            if (e.getDistanceFromDragStart() < 3 || anchor < 0) return;
            const auto& a = origs[(size_t) anchor];
            const double aAbs = clipStart() + a.start - offset();
            const double dt = snap (aAbs + (e.position.x - downPos.x) / pps, snapOn) - aAbs;
            const int dp = pitchFor (e.position.y) - pitchFor (downPos.y);
            const int prevPitch = notes[(size_t) anchor].pitch;
            for (size_t i = 0; i < notes.size(); ++i)
                if (selected[i])
                {
                    notes[i].start = juce::jmax (offset() - clipStart(), origs[i].start + dt);
                    notes[i].pitch = juce::jlimit (0, 127, origs[i].pitch + dp);
                }
            if (notes[(size_t) anchor].pitch != prevPitch) audition (notes[(size_t) anchor].pitch, notes[(size_t) anchor].velocity);
            commit();
            break;
        }
        case Drag::resize:
        case Drag::draw:
        {
            if (anchor < 0) return;
            const auto& a = origs[(size_t) anchor];
            const double aAbs = clipStart() + a.start - offset();
            const double minLen = juce::jmax (0.01, gridSec() > 0.0 ? gridSec() * 0.5 : 0.02);
            double newEnd = snap (aAbs + a.length + (e.position.x - downPos.x) / pps, snapOn);
            if (drag == Drag::draw) newEnd = juce::jmax (aAbs + newNoteLength(), snap (timeFor (e.position.x), snapOn));
            const double dl = (newEnd - aAbs) - a.length;
            for (size_t i = 0; i < notes.size(); ++i)
                if (selected[i]) notes[i].length = juce::jmax (minLen, origs[i].length + dl);
            commit();
            break;
        }
        case Drag::velocity:
        {
            const auto v = velArea();
            const int target = juce::jlimit (1, 127, juce::roundToInt (127.0f * ((float) v.getBottom() - e.position.y) / (float) (v.getHeight() - 6)));
            const int delta = target - origs[(size_t) anchor].velocity;
            for (size_t i = 0; i < notes.size(); ++i)
                if (selected[i]) notes[i].velocity = juce::jlimit (1, 127, origs[i].velocity + delta);
            velTip = notes[(size_t) anchor].velocity;
            velTipPos = e.position;
            commit();
            break;
        }
        case Drag::marquee:
        {
            marquee = juce::Rectangle<float> (downPos, e.position);
            for (size_t i = 0; i < notes.size(); ++i)
                if (noteRect (notes[i]).intersects (marquee)) selected[i] = 1;
                else if (! e.mods.isShiftDown()) selected[i] = 0;
            updateInfo();
            repaint();
            break;
        }
        case Drag::none: break;
    }
}

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
    stopAudition();
    marquee = {};
    velTip = -1;
    drag = Drag::none;
    repaint();
}

void PianoRoll::mouseMove (const juce::MouseEvent& e)
{
    bool edge = false;
    const int hit = gridArea().contains (e.getPosition()) ? noteAt (e.position, edge) : -1;
    if (rulerArea().contains (e.getPosition())) { setMouseCursor (juce::MouseCursor::PointingHandCursor); return; }
    if (velArea().contains (e.getPosition())) { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); return; }
    setMouseCursor (hit >= 0 ? (edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor)
                             : drawButton.getToggleState() && gridArea().contains (e.getPosition()) ? juce::MouseCursor::CrosshairCursor
                                                                                                   : juce::MouseCursor::NormalCursor);
}

void PianoRoll::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! gridArea().contains (e.getPosition()) || drawButton.getToggleState()) return;
    bool edge = false;
    const int hit = noteAt (e.position, edge);
    if (hit >= 0)
    {
        notes.erase (notes.begin() + hit);
        selected.assign (notes.size(), 0);
        commit ("Delete note");
        return;
    }
    midiclip::Note n;
    n.start = snap (timeFor (e.position.x), ! e.mods.isAltDown()) - clipStart() + offset();
    n.length = newNoteLength();
    n.pitch = pitchFor (e.position.y);
    n.velocity = 100;
    notes.push_back (n);
    selected.assign (notes.size(), 0);
    selected.back() = 1;
    commit ("Add note");
    audition (n.pitch, n.velocity);
    juce::Timer::callAfterDelay (250, [safe = juce::Component::SafePointer<PianoRoll> (this)] { if (safe != nullptr) safe->stopAudition(); });
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown())   // zoom around the mouse
    {
        const double t = timeFor (e.position.x);
        pps = juce::jlimit (10.0, 4000.0, pps * (w.deltaY > 0 ? 1.2 : 1.0 / 1.2));
        scrollT = t - (e.position.x - (float) gridArea().getX()) / pps;
    }
    else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
        scrollT -= (std::abs (w.deltaX) > 0.0f ? w.deltaX : w.deltaY) * 300.0 / pps;
    else
        topPitch = juce::jlimit ((float) (gridArea().getHeight() / keyH) - 1.0f, 127.0f, topPitch + w.deltaY * 12.0f);
    repaint();
}

bool PianoRoll::keyPressed (const juce::KeyPress& k)
{
    const auto c = k.getKeyCode();
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { deleteSelected(); return true; }
    if (k.getModifiers().isCommandDown() && (c == 'A' || c == 'a')) { std::fill (selected.begin(), selected.end(), 1); updateInfo(); repaint(); return true; }
    if (k.getModifiers().isCommandDown() && (c == 'Z' || c == 'z'))
    {
        if (k.getModifiers().isShiftDown()) doc.undo().redo(); else doc.undo().undo();
        return true;
    }
    if (k.getModifiers().isCommandDown() && (c == 'Y' || c == 'y')) { doc.undo().redo(); return true; }
    if (k.getModifiers().isCommandDown() && (c == 'C' || c == 'c')) { copySelected(); return true; }
    if (k.getModifiers().isCommandDown() && (c == 'X' || c == 'x')) { if (numSelected() > 0) { copySelected(); deleteSelected(); } return true; }
    if (k.getModifiers().isCommandDown() && (c == 'V' || c == 'v')) { pasteAt (snap (host.engine.getPositionSeconds(), true)); return true; }
    if (k.getModifiers().isCommandDown() && (c == 'D' || c == 'd')) { duplicateSelected(); return true; }
    if (c == juce::KeyPress::upKey) { transpose (k.getModifiers().isShiftDown() ? 12 : 1); return true; }
    if (c == juce::KeyPress::downKey) { transpose (k.getModifiers().isShiftDown() ? -12 : -1); return true; }
    if (c == juce::KeyPress::leftKey) { nudge (-1); return true; }
    if (c == juce::KeyPress::rightKey) { nudge (1); return true; }
    if (c == 'Q' || c == 'q') { quantize(); return true; }
    if (c == 'D' || c == 'd') { drawButton.setToggleState (! drawButton.getToggleState(), juce::dontSendNotification); return true; }
    if (k == juce::KeyPress::spaceKey)
    {
        host.engine.togglePlay();
        return true;
    }
    return false;
}

// ------------------------------------------------------------------ updates

void PianoRoll::timerCallback()
{
    if (! fitted && isShowing() && gridArea().getWidth() > 200 && gridArea().getHeight() > 100) { fitted = true; fit(); }
    if (! clip.getParent().isValid()) { stopTimer(); if (onClipGone) onClipGone(); return; }
    if (flashTicks > 0 && --flashTicks == 0) updateInfo();
    const double ph = host.engine.getPositionSeconds();
    if (ph != lastPlayhead)
    {
        lastPlayhead = ph;
        // keep the playhead in view while playing
        const double view = gridArea().getWidth() / pps;
        if (host.engine.isPlaying() && drag == Drag::none && (ph > scrollT + view * 0.95 || ph < scrollT))
            scrollT = ph - view * 0.05;
        repaint();
    }
}

void PianoRoll::valueTreePropertyChanged (juce::ValueTree& node, const juce::Identifier&)
{
    if (writing) return;
    if (node == clip || node.hasType (midiclip::MIDI)) load();   // undo / redo, or edited elsewhere
}

void PianoRoll::valueTreeParentChanged (juce::ValueTree&)
{
    if (! clip.getParent().isValid() && onClipGone)
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PianoRoll> (this)] { if (safe != nullptr && safe->onClipGone) safe->onClipGone(); });
}
} // namespace ff
