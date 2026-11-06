#include "ArrangeView.h"
#include "core/MidiClip.h"

namespace ff
{
using namespace timeline;
namespace pids = project::ids;

static double prop (const juce::ValueTree& v, const juce::Identifier& id) { return (double) v[id]; }

// =====================================================================================
// TrackHeader
// =====================================================================================

static void styleToggle (juce::TextButton& b, juce::Colour on, const juce::String& tip)
{
    b.setColour (juce::TextButton::buttonOnColourId, on);
    b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    b.setTooltip (tip);
    b.setWantsKeyboardFocus (false);
}

TrackHeader::TrackHeader (ArrangeView& v, juce::ValueTree t, int i) : track (t), index (i), owner (v)
{
    name.setFont (uiFont (14.0f, true));
    name.setColour (juce::Label::textColourId, palette::text);
    name.setEditable (false, true, false);
    name.onTextChange = [this] {
        const auto n = name.getText().trim();
        if (n.isNotEmpty() && n != track[ids::name].toString())
        {
            owner.doc.undo().beginNewTransaction ("Rename track");
            owner.doc.set (track, ids::name, n);
        }
    };
    name.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (name);

    styleToggle (arm, palette::bad, "Arm for recording");
    styleToggle (mon, palette::info, "Input monitoring: hear the input through this track");
    styleToggle (mute, palette::accent, "Mute");
    styleToggle (solo, palette::warn, "Solo");
    // With several tracks selected, these buttons switch the whole selection.
    arm.onClick = [this] { owner.session.setOnTracks (track, ids::armed, ! (bool) track[ids::armed], false); };
    mon.onClick = [this] { owner.session.setOnTracks (track, ids::monitor, ! (bool) track[ids::monitor], false); };
    mute.onClick = [this] { owner.session.setOnTracks (track, ids::mute, ! (bool) track[ids::mute], false); };
    solo.onClick = [this] { owner.session.setOnTracks (track, ids::solo, ! (bool) track[ids::solo], false); };
    for (auto* b : { &arm, &mon, &mute, &solo })
        addAndMakeVisible (b);

    input.setTooltip ("Recording / monitoring input");
    input.onChange = [this] {
        const int i2 = input.getSelectedItemIndex();
        if (updating || ! juce::isPositiveAndBelow (i2, inputValues.size())) return;
        owner.doc.undo().beginNewTransaction ("Track input");
        owner.doc.set (track, ids::inputChannel, inputValues[i2].first);
        owner.doc.set (track, ids::inputStereo, inputValues[i2].second);
    };
    addAndMakeVisible (input);
    route.setTooltip ("Where this bus / FX track plays into, and its sends");
    laneButton.onClick = [this] { owner.showLaneChooser (track, &laneButton); };
    addChildComponent (laneButton);
    route.onClick = [this] {
        if (! isInstrumentTrack (track)) { owner.session.showRoutingMenu (track, &route); return; }
        if (juce::ModifierKeys::currentModifiers.isPopupMenu() || track.getChildWithName (midiclip::INSTRUMENT)[rig::ids::desc].toString().isEmpty())
            owner.session.showInstrumentMenu (track, &route);
        else owner.session.openInstrument (track);
    };
    addChildComponent (route);

    gain.setSliderStyle (juce::Slider::LinearHorizontal);
    gain.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    gain.setRange (-60.0, 6.0, 0.1);
    gain.setSkewFactorFromMidPoint (-12.0);
    gain.setDoubleClickReturnValue (true, 0.0);
    gain.setPopupDisplayEnabled (true, true, nullptr);
    gain.setTextValueSuffix (" dB");
    gain.setTooltip ("Volume (double-click: 0 dB)");
    gain.onDragStart = [this] { owner.doc.undo().beginNewTransaction ("Track volume"); beginGroupDrag (ids::gainDb); };
    gain.onValueChange = [this] {
        if (updating) return;
        const double before = (double) track[ids::gainDb];
        owner.doc.set (track, ids::gainDb, gain.getValue());
        applyGroupDelta (ids::gainDb, gain.getValue() - before, -60.0, 6.0);
    };
    addAndMakeVisible (gain);

    pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    pan.setRange (-1.0, 1.0, 0.01);
    pan.getProperties().set ("pan", true);
    pan.setDoubleClickReturnValue (true, 0.0);
    pan.setPopupDisplayEnabled (true, true, nullptr);
    pan.setTooltip ("Pan (double-click: centre)");
    pan.onDragStart = [this] { owner.doc.undo().beginNewTransaction ("Track pan"); beginGroupDrag (ids::pan); };
    pan.onValueChange = [this] {
        if (updating) return;
        const double before = (double) track[ids::pan];
        owner.doc.set (track, ids::pan, pan.getValue());
        applyGroupDelta (ids::pan, pan.getValue() - before, -1.0, 1.0);
    };
    addAndMakeVisible (pan);

    track.addListener (this);
    refreshInputs();
    refresh();
}

TrackHeader::~TrackHeader() { track.removeListener (this); }

void TrackHeader::refreshInputs()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    const auto names = owner.session.inputChoiceNames (inputValues);
    input.clear (juce::dontSendNotification);
    for (int i = 0; i < names.size(); ++i)
        input.addItem (names[i], i + 1);
    refresh();
}

void TrackHeader::refreshAutomation()
{
    const bool a = owner.automationMode();
    laneButton.setVisible (a);
    input.setVisible (! a && isAudioTrack (track));
    route.setVisible (! a && ! isAudioTrack (track));
    if (a)
    {
        const auto prm = owner.laneParamFor (track);
        laneButton.setButtonText ("A: " + owner.laneLabel (track, prm));
        laneButton.setTooltip ("Automation lane shown for this track (click to pick volume, pan or a plugin parameter)");
        input.setVisible (false);
        route.setVisible (false);
    }
}

void TrackHeader::refresh()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    name.setText (track[ids::name].toString(), juce::dontSendNotification);
    arm.setToggleState ((bool) track[ids::armed], juce::dontSendNotification);
    const bool audio = isAudioTrack (track), inst = isInstrumentTrack (track);
    arm.setVisible (audio || inst);
    arm.setTooltip (inst ? "Play this instrument from your MIDI keyboard" : "Arm for recording");
    mon.setVisible (audio);
    input.setVisible (audio);
    route.setVisible (! audio);
    if (inst) route.setButtonText ("SYNTH  " + owner.session.instrumentName (track));
    else route.setButtonText ((kindOf (track) == TrackKind::fx ? "FX  > " : "BUS  > ") + owner.session.outputName (track));
    route.setTooltip (inst ? "Click to open the instrument; right-click to choose another" : "Where this bus / FX track plays into, and its sends");
    refreshAutomation();
    mon.setToggleState ((bool) track[ids::monitor], juce::dontSendNotification);
    mute.setToggleState ((bool) track[ids::mute], juce::dontSendNotification);
    solo.setToggleState ((bool) track[ids::solo], juce::dontSendNotification);
    gain.setValue (prop (track, ids::gainDb), juce::dontSendNotification);
    pan.setValue (prop (track, ids::pan), juce::dontSendNotification);
    const std::pair<int, bool> cur { (int) track[ids::inputChannel], (bool) track[ids::inputStereo] };
    int sel = 0;
    for (int i = 0; i < inputValues.size(); ++i)
        if (inputValues[i] == cur) sel = i;
    input.setSelectedItemIndex (sel, juce::dontSendNotification);
    repaint();
}

void TrackHeader::beginGroupDrag (const juce::Identifier&)
{
    groupStart.clear();
}

void TrackHeader::applyGroupDelta (const juce::Identifier& id, double delta, double lo, double hi)
{
    // Relative moves keep the balance between selected tracks (like dragging a grouped fader).
    if (std::abs (delta) < 1.0e-9) return;
    for (auto t : owner.session.linkedTracks (track))
        if (t != track)
            owner.doc.set (t, id, juce::jlimit (lo, hi, (double) t[id] + delta));
}

void TrackHeader::tickMeter()
{
    auto& m = owner.host.engine.trackMeters[(size_t) juce::jlimit (0, maxTracks - 1, index)];
    m.uiTake();
    const float db = gainToDb (m.uiPeak);
    meterLevel = db > meterLevel ? db : juce::jmax (db, meterLevel - 1.6f);
    repaint (getLocalBounds().removeFromRight (8));
}

void TrackHeader::paint (juce::Graphics& g)
{
    const bool selected = owner.session.isTrackSelected (track);
    auto r = getLocalBounds();
    g.setColour (selected ? palette::panelHi : palette::panel);
    g.fillRect (r);
    g.setColour (palette::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    g.setColour (owner.trackColour (track));
    g.fillRect (r.removeFromLeft (5));
    if (const auto grp = track[ids::group].toString(); grp.isNotEmpty())   // track group stripe
    {
        g.setColour (SessionController::groupColour (grp));
        g.fillRect (r.removeFromLeft (3));
    }
    if (selected)
    {
        g.setColour (palette::accent.withAlpha (0.8f));
        g.drawRect (getLocalBounds().withTrimmedLeft (5), 1);
    }
    if ((bool) track[ids::armed])
    {
        g.setColour (palette::bad.withAlpha (0.12f));
        g.fillRect (r);
    }

    auto meter = getLocalBounds().removeFromRight (8).reduced (2, 6);
    g.setColour (palette::well);
    g.fillRect (meter);
    const float n = juce::jlimit (0.0f, 1.0f, (meterLevel + 60.0f) / 60.0f);
    g.setColour (meterLevel > -0.5f ? palette::bad : (meterLevel > -9.0f ? palette::warn : palette::good));
    g.fillRect (meter.withTop (meter.getBottom() - juce::roundToInt (n * meter.getHeight())));
}

void TrackHeader::resized()
{
    auto r = getLocalBounds().reduced (10, 6).withTrimmedRight (6);
    auto top = r.removeFromTop (24);
    const int bw = 24;
    solo.setBounds (top.removeFromRight (bw).reduced (1));
    mute.setBounds (top.removeFromRight (bw).reduced (1));
    mon.setBounds (top.removeFromRight (bw).reduced (1));
    arm.setBounds (top.removeFromRight (bw).reduced (1));
    name.setBounds (top.withTrimmedRight (4));
    r.removeFromTop (6);
    auto row = r.removeFromTop (24);
    pan.setBounds (row.removeFromRight (26));
    row.removeFromRight (4);
    input.setBounds (row.removeFromLeft (row.getWidth() / 2 + 10));
    route.setBounds (input.getBounds());
    laneButton.setBounds (input.getBounds());
    row.removeFromLeft (4);
    gain.setBounds (row);
}

void TrackHeader::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (! owner.session.isTrackSelected (track))
            owner.session.clickTrack (track, {});
        showMenu();
        return;
    }
    owner.session.clickTrack (track, e.mods);
    if (e.getNumberOfClicks() == 2)
        name.showEditor();
}

void TrackHeader::mouseDrag (const juce::MouseEvent& e)
{
    // Drag a header up/down to move the track (and any other selected tracks) to a new position.
    if (e.mods.isPopupMenu() || std::abs (e.getDistanceFromDragStartY()) < 5) return;
    draggingTrack = true;
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    const int y = e.getEventRelativeTo (getParentComponent()).y;
    const int idx = juce::jlimit (0, owner.numTracks(), juce::roundToInt ((float) (y + owner.geo.scrollY) / (float) owner.geo.trackHeight));
    owner.setTrackDropIndex (idx);
}

void TrackHeader::mouseUp (const juce::MouseEvent&)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
    if (! draggingTrack) return;
    draggingTrack = false;
    const int idx = owner.trackDropIndex();
    owner.setTrackDropIndex (-1);
    if (idx >= 0)
        owner.moveTracks (owner.session.linkedTracks (track), idx);   // a group moves as one block
}

void TrackHeader::showMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Rename...");
    juce::PopupMenu colours;
    for (int i = 0; i < trackPalette().size(); ++i)
        colours.addColouredItem (100 + i, "Colour " + juce::String (i + 1), juce::Colour::fromString (trackPalette()[i]));
    m.addSubMenu ("Colour", colours);
    owner.session.addGroupMenu (m, track, 700);
    m.addSeparator();
    if (! isAudioTrack (track)) {}
    else if (owner.session.trackHasRig (track))
    {
        m.addItem (10, "Open NAM amp...");
        m.addItem (11, "Remove NAM amp");
    }
    else m.addItem (10, "Add NAM amp...");
    m.addItem (12, "Add plugin...", owner.session.plugins != nullptr && rig::numInserts (track) < rig::maxInserts);
    m.addItem (13, "Output and sends  (now: " + owner.session.outputName (track) + ")...");
    if (isInstrumentTrack (track))
    {
        m.addItem (14, "Choose instrument  (now: " + owner.session.instrumentName (track) + ")...");
        m.addItem (15, "Open instrument");
    }
    for (int i = 0; i < rig::numInserts (track); ++i)
        m.addItem (200 + i, "Plugin: " + rig::insertNode (track, i)[rig::ids::name].toString()
                                + ((bool) rig::insertNode (track, i)[rig::ids::bypass] ? "  (bypassed)" : "") + "...");
    m.addSeparator();
    m.addItem (2, "Move up", index > 0);
    m.addItem (3, "Move down", index < owner.numTracks() - 1);
    const int n = owner.session.tracksForEdit (track).size();
    m.addItem (4, n > 1 ? "Duplicate " + juce::String (n) + " tracks" : juce::String ("Duplicate track"));
    m.addSeparator();
    m.addItem (6, "Select all tracks");
    m.addItem (5, n > 1 ? "Delete " + juce::String (n) + " tracks" : juce::String ("Delete track"));
    juce::Component::SafePointer<TrackHeader> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r) {
        if (safe == nullptr || r == 0) return;
        auto& o = safe->owner;
        auto t = safe->track;
        auto tracks = o.doc.tracks();
        const int idx = tracks.indexOf (t);
        if (o.session.handleGroupMenu (r, t, 700)) return;
        if (r == 1) safe->name.showEditor();
        else if (r == 10) o.session.addTrackRig (t);
        else if (r == 11) o.session.removeTrackRig (t);
        else if (r == 12) o.session.showAddInsertMenu (t, safe.getComponent());
        else if (r == 13) o.session.showRoutingMenu (t, safe.getComponent());
        else if (r == 14) o.session.showInstrumentMenu (t, safe.getComponent());
        else if (r == 15) o.session.openInstrument (t);
        else if (r >= 200 && r < 200 + rig::maxInserts) o.session.showInsertMenu (t, r - 200, safe.getComponent());
        else if (r >= 100) { o.doc.undo().beginNewTransaction ("Track colour"); o.doc.set (t, ids::colour, trackPalette()[r - 100]); }
        else if (r == 2) { o.doc.undo().beginNewTransaction ("Move track"); tracks.moveChild (idx, idx - 1, &o.doc.undo()); }
        else if (r == 3) { o.doc.undo().beginNewTransaction ("Move track"); tracks.moveChild (idx, idx + 1, &o.doc.undo()); }
        else if (r == 4) { for (auto x : o.session.tracksForEdit (t)) o.session.duplicateTrack (x); }
        else if (r == 5) { for (auto x : o.session.tracksForEdit (t)) o.session.deleteTrack (x); }
        else if (r == 6)
        {
            juce::Array<juce::ValueTree> all;
            for (auto x : o.doc.tracks()) all.add (x);
            o.session.setSelectedTracks (all);
        }
    });
}

// =====================================================================================
// Ruler
// =====================================================================================

juce::ValueTree Ruler::markerAt (float x) const
{
    for (auto m : owner.doc.markers())
        if (std::abs (owner.geo.xFor (prop (m, ids::time)) - x) < 6.0f)
            return m;
    return {};
}

void Ruler::paint (juce::Graphics& g)
{
    const auto& geo = owner.geo;
    g.fillAll (palette::panel);
    auto r = getLocalBounds();
    auto markersArea = r.removeFromBottom (markerLane);
    g.setColour (palette::well);
    g.fillRect (markersArea);

    // loop range
    auto t = owner.doc.transport();
    const float lx0 = geo.xFor (prop (t, pids::loopStart)), lx1 = geo.xFor (prop (t, pids::loopEnd));
    const bool loopOn = (bool) t[pids::loopEnabled];
    g.setColour ((loopOn ? palette::accent : palette::textDim).withAlpha (loopOn ? 0.55f : 0.25f));
    g.fillRect (juce::Rectangle<float> (lx0, 2.0f, lx1 - lx0, 8.0f));
    if ((bool) t[pids::punchEnabled])
    {
        g.setColour (palette::bad);
        g.fillRect (juce::Rectangle<float> (lx0, 2.0f, 2.0f, (float) r.getHeight()));
        g.fillRect (juce::Rectangle<float> (lx1 - 2.0f, 2.0f, 2.0f, (float) r.getHeight()));
    }

    // bars and beats
    const double bar = geo.barSec();
    const double pxPerBar = bar * geo.pixelsPerSecond;
    int labelEvery = 1;
    while (pxPerBar * labelEvery < 50.0) labelEvery *= 2;
    const int firstBar = juce::jmax (0, (int) std::floor (geo.scrollX / bar));
    const int lastBar = (int) std::ceil (geo.timeFor ((float) getWidth()) / bar);
    const double beat = geo.quarterSec() * 4.0 / geo.tsDen;
    g.setFont (uiFont (11.0f));
    for (int b = firstBar; b <= lastBar; ++b)
    {
        const float x = geo.xFor (b * bar);
        g.setColour (palette::textDim);
        g.drawVerticalLine ((int) x, (float) r.getBottom() - 14.0f, (float) r.getBottom());
        if (b % labelEvery == 0)
            g.drawText (juce::String (b + 1), (int) x + 3, r.getY() + 10, 40, 14, juce::Justification::left);
        if (beat * geo.pixelsPerSecond > 8.0)
            for (int k = 1; k < geo.tsNum; ++k)
            {
                const float bx = geo.xFor (b * bar + k * beat);
                g.setColour (palette::outline);
                g.drawVerticalLine ((int) bx, (float) r.getBottom() - 6.0f, (float) r.getBottom());
            }
    }

    // markers
    g.setFont (uiFont (11.0f, true));
    for (auto m : owner.doc.markers())
    {
        const float x = geo.xFor (prop (m, ids::time));
        if (x < -100.0f || x > (float) getWidth()) continue;
        juce::Path p;
        p.addTriangle (x - 5.0f, (float) markersArea.getY() + 2.0f, x + 5.0f, (float) markersArea.getY() + 2.0f, x, (float) markersArea.getY() + 10.0f);
        g.setColour (palette::info);
        g.fillPath (p);
        g.drawText (m[ids::name].toString(), (int) x + 7, markersArea.getY(), 140, markersArea.getHeight(), juce::Justification::centredLeft);
    }

    // playhead
    const float px = geo.xFor (owner.playhead());
    g.setColour (palette::accent);
    juce::Path ph;
    ph.addTriangle (px - 6.0f, (float) r.getBottom() - 10.0f, px + 6.0f, (float) r.getBottom() - 10.0f, px, (float) r.getBottom());
    g.fillPath (ph);
    g.setColour (palette::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void Ruler::mouseDown (const juce::MouseEvent& e)
{
    dragStart = -1.0;
    if (e.y >= getHeight() - markerLane)
    {
        auto m = markerAt ((float) e.x);
        if (! m.isValid())
            return;
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            menu.addItem (1, "Rename marker...");
            menu.addItem (2, "Delete marker");
            juce::Component::SafePointer<Ruler> safe (this);
            menu.showMenuAsync ({}, [safe, m] (int r) mutable {
                if (safe == nullptr) return;
                auto& o = safe->owner;
                if (r == 1)
                    o.renameDialog ("Rename marker", m[ids::name].toString(), [&o, m] (juce::String n) mutable {
                        o.doc.undo().beginNewTransaction ("Rename marker");
                        o.doc.set (m, ids::name, n);
                    });
                if (r == 2) { o.doc.undo().beginNewTransaction ("Delete marker"); o.doc.markers().removeChild (m, &o.doc.undo()); }
            });
            return;
        }
        owner.locate (prop (m, ids::time));
        return;
    }
    dragStart = owner.geo.snap (owner.geo.timeFor ((float) e.x), ! e.mods.isAltDown());
}

void Ruler::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStart < 0.0 || e.getDistanceFromDragStartX() == 0 || std::abs (e.getDistanceFromDragStartX()) < 4)
        return;
    const double t = owner.geo.snap (owner.geo.timeFor ((float) e.x), ! e.mods.isAltDown());
    auto tr = owner.doc.transport();
    const double a = juce::jmin (dragStart, t), b = juce::jmax (dragStart, t);
    if (b - a < 0.01) return;
    if (! e.mouseWasDraggedSinceMouseDown() || owner.doc.undo().getCurrentTransactionName() != "Set loop range")
        owner.doc.undo().beginNewTransaction ("Set loop range");
    owner.doc.set (tr, pids::loopStart, a);
    owner.doc.set (tr, pids::loopEnd, b);
    owner.doc.set (tr, pids::loopEnabled, true);
    repaint();
}

void Ruler::mouseUp (const juce::MouseEvent& e)
{
    if (dragStart >= 0.0 && std::abs (e.getDistanceFromDragStartX()) < 4 && e.y < getHeight() - markerLane)
        owner.locate (owner.geo.snap (owner.geo.timeFor ((float) e.x), ! e.mods.isAltDown()));
    dragStart = -1.0;
}

void Ruler::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.y >= getHeight() - markerLane && ! markerAt ((float) e.x).isValid())
    {
        owner.doc.undo().beginNewTransaction ("Add marker");
        owner.doc.markers().appendChild (createMarker ("Marker " + juce::String (owner.doc.markers().getNumChildren() + 1),
                                                       owner.geo.snap (owner.geo.timeFor ((float) e.x))), &owner.doc.undo());
    }
}

// =====================================================================================
// TimelineCanvas
// =====================================================================================

const std::vector<midiclip::Note>& TimelineCanvas::cachedNotes (const juce::ValueTree& clip)
{
    const auto text = clip.getChildWithName (midiclip::MIDI)[midiclip::notes].toString();
    auto& e = noteCache[clip[ids::id].toString()];
    if (e.first != text) { e.first = text; e.second = midiclip::notesOf (clip); }
    return e.second;
}

void TimelineCanvas::drawClip (juce::Graphics& g, const juce::ValueTree& clip, juce::Rectangle<float> r, juce::Colour colour,
                               bool selected)
{
    const bool muted = (bool) clip[ids::muted];
    const float alpha = muted ? 0.45f : 1.0f;
    g.setColour (palette::bg.interpolatedWith (colour, 0.28f).withAlpha (alpha));
    g.fillRoundedRectangle (r, 4.0f);
    auto header = r.withHeight (15.0f);
    g.setColour (colour.withAlpha (0.85f * alpha));
    g.fillRoundedRectangle (header, 4.0f);
    g.fillRect (header.withTrimmedTop (8.0f));

    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.setFont (uiFont (11.0f, true));
    g.drawText ((muted ? "[muted] " : "") + clip[ids::name].toString(), header.reduced (5.0f, 0.0f).toNearestInt(),
                juce::Justification::centredLeft, true);

    const auto& geo = owner.geo;
    const double start = prop (clip, ids::start), len = prop (clip, ids::length), offset = prop (clip, ids::offset);
    auto wave = r.withTrimmedTop (16.0f).reduced (0.0f, 2.0f);
    if (midiclip::isMidiClip (clip))   // MIDI: a mini piano roll
    {
        const auto& notes = cachedNotes (clip);
        int lo = 0, hi = 0;
        if (midiclip::pitchRange (notes, lo, hi) && wave.getHeight() > 4.0f)
        {
            lo = juce::jmin (lo, hi - 11);   // at least an octave tall
            const float rowH = wave.getHeight() / (float) (hi - lo + 1);
            g.setColour (colour.brighter (0.5f).withAlpha (0.95f * alpha));
            for (const auto& n : notes)
            {
                const double t0 = start + (n.start - offset), t1 = t0 + n.length;
                if (t1 <= start || t0 >= start + len) continue;
                const float x0 = juce::jmax (r.getX(), geo.xFor (juce::jmax (t0, start)));
                const float x1 = juce::jmin (r.getRight(), geo.xFor (juce::jmin (t1, start + len)));
                if (x1 < 0.0f || x0 > (float) getWidth()) continue;
                const float y = wave.getBottom() - (float) (n.pitch - lo + 1) * rowH;
                g.fillRect (x0, y, juce::jmax (1.5f, x1 - x0 - 1.0f), juce::jmax (1.5f, rowH - 0.5f));
            }
        }
        else
        {
            g.setColour (palette::textDim);
            g.setFont (uiFont (11.0f));
            g.drawText ("Empty MIDI clip", wave.toNearestInt(), juce::Justification::centred, true);
        }
        juce::ignoreUnused (selected);
        return;
    }
    auto audio = owner.session.audioFor (clip);
    if (audio == nullptr)
    {
        g.setColour (palette::textDim);
        g.setFont (uiFont (11.0f));
        const bool missing = ! owner.session.fileFor (clip).existsAsFile();
        g.drawText (missing ? "Missing file: " + clip[ids::file].toString() : juce::String ("Loading..."), wave.toNearestInt(),
                    juce::Justification::centred, true);
    }
    else if (wave.getHeight() > 6.0f)
    {
        const float mid = wave.getCentreY(), half = wave.getHeight() * 0.5f;
        const float clipGain = juce::Decibels::decibelsToGain ((float) prop (clip, ids::gainDb));
        const int x0 = juce::jmax ((int) r.getX(), 0), x1 = juce::jmin ((int) r.getRight(), getWidth());
        const double sr = audio->sampleRate;
        const int ch = audio->numChannels();
        const auto blocks = (juce::int64) audio->peaks.size() / (ch * 2);
        g.setColour (colour.brighter (0.4f).withAlpha (0.9f * alpha));
        for (int x = x0; x < x1; ++x)
        {
            const double s0 = (offset + (geo.timeFor ((float) x) - start)) * sr;
            const double s1 = (offset + (geo.timeFor ((float) x + 1.0f) - start)) * sr;
            if (s1 <= 0 || s0 >= audio->length()) continue;
            float lo = 0.0f, hi = 0.0f;
            if (s1 - s0 < CachedAudio::peakBlock)
            {
                const int a = juce::jlimit (0, (int) audio->length() - 1, (int) s0), b = juce::jlimit (a + 1, (int) audio->length(), (int) s1 + 1);
                for (int c = 0; c < ch; ++c)
                {
                    auto mm = juce::FloatVectorOperations::findMinAndMax (audio->buffer.getReadPointer (c, a), b - a);
                    lo = juce::jmin (lo, mm.getStart()); hi = juce::jmax (hi, mm.getEnd());
                }
            }
            else
            {
                const auto b0 = juce::jlimit ((juce::int64) 0, blocks - 1, (juce::int64) (s0 / CachedAudio::peakBlock));
                const auto b1 = juce::jlimit (b0 + 1, blocks, (juce::int64) (s1 / CachedAudio::peakBlock) + 1);
                for (auto b = b0; b < b1; ++b)
                    for (int c = 0; c < ch; ++c)
                    {
                        lo = juce::jmin (lo, audio->peaks[(size_t) ((b * ch + c) * 2)]);
                        hi = juce::jmax (hi, audio->peaks[(size_t) ((b * ch + c) * 2 + 1)]);
                    }
            }
            const double t = geo.timeFor ((float) x);
            const float fade = (float) (fadeGain (t, start, start + prop (clip, ids::fadeIn), true)
                                        * fadeGain (t, start + len - prop (clip, ids::fadeOut), start + len, false));
            const float k = clipGain * fade * half;
            g.drawVerticalLine (x, mid - juce::jmin (half, hi * k), mid - juce::jmax (-half, lo * k) + 1.0f);
        }
    }

    // fades
    const float fiW = (float) (prop (clip, ids::fadeIn) * geo.pixelsPerSecond);
    const float foW = (float) (prop (clip, ids::fadeOut) * geo.pixelsPerSecond);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    if (fiW > 1.0f)
    {
        juce::Path p;
        p.addTriangle (r.getX(), r.getY() + 15.0f, r.getX() + fiW, r.getY() + 15.0f, r.getX(), r.getBottom());
        g.fillPath (p);
    }
    if (foW > 1.0f)
    {
        juce::Path p;
        p.addTriangle (r.getRight(), r.getY() + 15.0f, r.getRight() - foW, r.getY() + 15.0f, r.getRight(), r.getBottom());
        g.fillPath (p);
    }
    if (selected || r.getWidth() > 30.0f)
    {
        g.setColour (palette::text.withAlpha (0.8f));
        g.fillRect (juce::Rectangle<float> (r.getX() + fiW - 3.0f, r.getY() + 1.0f, 6.0f, 6.0f));
        g.fillRect (juce::Rectangle<float> (r.getRight() - foW - 3.0f, r.getY() + 1.0f, 6.0f, 6.0f));
    }

    g.setColour (selected ? palette::text : colour.darker (0.3f));
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, selected ? 2.0f : 1.0f);
}

void TimelineCanvas::paint (juce::Graphics& g)
{
    const auto& geo = owner.geo;
    g.fillAll (palette::bg);
    const int n = owner.numTracks();
    const int th = geo.trackHeight;

    // lanes
    for (int i = 0; i < n; ++i)
    {
        const int y = i * th - geo.scrollY;
        if (y > getHeight() || y + th < 0) continue;
        auto t = owner.trackAtIndex (i);
        g.setColour (owner.session.isTrackSelected (t) ? palette::panel.brighter (0.04f) : (i % 2 ? palette::bg : palette::bg.brighter (0.02f)));
        g.fillRect (0, y, getWidth(), th);
        g.setColour (palette::outline.withAlpha (0.6f));
        g.drawHorizontalLine (y + th - 1, 0.0f, (float) getWidth());
    }

    // grid
    const double bar = geo.barSec(), beat = geo.quarterSec() * 4.0 / geo.tsDen;
    const int firstBar = juce::jmax (0, (int) std::floor (geo.scrollX / bar));
    const int lastBar = (int) std::ceil (geo.timeFor ((float) getWidth()) / bar);
    for (int b = firstBar; b <= lastBar; ++b)
    {
        g.setColour (palette::outline.withAlpha (0.7f));
        g.drawVerticalLine ((int) geo.xFor (b * bar), 0.0f, (float) getHeight());
        if (beat * geo.pixelsPerSecond > 14.0)
        {
            g.setColour (palette::outline.withAlpha (0.25f));
            for (int k = 1; k < geo.tsNum; ++k)
                g.drawVerticalLine ((int) geo.xFor (b * bar + k * beat), 0.0f, (float) getHeight());
        }
    }

    // loop region
    auto tr = owner.doc.transport();
    if ((bool) tr[pids::loopEnabled])
    {
        const float a = geo.xFor (prop (tr, pids::loopStart)), b = geo.xFor (prop (tr, pids::loopEnd));
        g.setColour (palette::accent.withAlpha (0.06f));
        g.fillRect (juce::Rectangle<float> (a, 0.0f, b - a, (float) getHeight()));
    }

    // clips (child order = layering: later children drawn on top)
    for (int i = 0; i < n; ++i)
    {
        const int y = i * th - geo.scrollY;
        if (y > getHeight() || y + th < 0) continue;
        auto t = owner.trackAtIndex (i);
        const auto colour = owner.trackColour (t);
        for (auto c : t)
        {
            if (! c.hasType (ids::CLIP)) continue;
            const float x0 = geo.xFor (prop (c, ids::start));
            const float x1 = geo.xFor (prop (c, ids::start) + prop (c, ids::length));
            if (x1 < 0.0f || x0 > (float) getWidth()) continue;
            drawClip (g, c, { x0, (float) y + 3.0f, juce::jmax (2.0f, x1 - x0), (float) th - 7.0f }, colour, owner.selection.contains (c));
        }
    }

    if (owner.automationMode()) drawAutomation (g);

    if (n == 0)
    {
        g.setColour (palette::textDim);
        g.setFont (uiFont (15.0f));
        g.drawFittedText ("No tracks yet.\nClick \"+ Track\" (Ctrl+T), or drag audio files here.", getLocalBounds(),
                          juce::Justification::centred, 3);
    }

    // recording in progress
    auto& e = owner.host.engine;
    if (owner.session.isRecordingSession() && e.isRecordingActive() && ! e.isCountingIn())
    {
        const double ph = owner.playhead();
        double from = owner.session.recordingStartSec();
        if (ph < from) from = (bool) tr[pids::loopEnabled] ? prop (tr, pids::loopStart) : 0.0;
        if ((bool) tr[pids::punchEnabled]) from = juce::jmax (from, prop (tr, pids::loopStart));
        for (auto& id : owner.session.recordingTrackIds())
        {
            const int i = owner.doc.tracks().indexOf (owner.session.findTrack (id));
            if (i < 0 || ph <= from) continue;
            const float x0 = geo.xFor (from), x1 = geo.xFor (ph);
            juce::Rectangle<float> r (x0, (float) (i * th - geo.scrollY) + 3.0f, x1 - x0, (float) th - 7.0f);
            g.setColour (palette::bad.withAlpha (0.35f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (palette::bad);
            g.drawRoundedRectangle (r, 4.0f, 1.5f);
        }
    }

    // markers
    for (auto m : owner.doc.markers())
    {
        const float x = geo.xFor (prop (m, ids::time));
        g.setColour (palette::info.withAlpha (0.5f));
        for (float y = 0; y < (float) getHeight(); y += 8.0f)
            g.drawVerticalLine ((int) x, y, y + 4.0f);
    }

    // drop indicator
    if (dropX >= 0)
    {
        g.setColour (palette::accent);
        g.drawVerticalLine ((int) geo.xFor (geo.snap (geo.timeFor ((float) dropX))), 0.0f, (float) getHeight());
    }

    // marquee
    if (mode == Mode::marquee && ! marqueeRect.isEmpty())
    {
        g.setColour (palette::accent.withAlpha (0.12f));
        g.fillRect (marqueeRect);
        g.setColour (palette::accent);
        g.drawRect (marqueeRect, 1);
    }

    // slice tool: where the cut goes
    if (owner.sliceMode() && sliceX >= 0.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        const float dashes[] = { 4.0f, 3.0f };
        g.drawDashedLine ({ sliceX, 0.0f, sliceX, (float) getHeight() }, dashes, 2, 1.5f);
    }

    // playhead
    g.setColour (e.isRecordingActive() ? palette::bad : palette::accent);
    g.drawVerticalLine ((int) geo.xFor (owner.playhead()), 0.0f, (float) getHeight());
}

TimelineCanvas::Mode TimelineCanvas::modeAt (const juce::ValueTree& clip, juce::Point<float> p) const
{
    const auto& geo = owner.geo;
    const float x0 = geo.xFor (prop (clip, ids::start));
    const float x1 = geo.xFor (prop (clip, ids::start) + prop (clip, ids::length));
    const int i = owner.doc.tracks().indexOf (clip.getParent());
    const float top = (float) (i * geo.trackHeight - geo.scrollY) + 3.0f;
    const float fiX = x0 + (float) (prop (clip, ids::fadeIn) * geo.pixelsPerSecond);
    const float foX = x1 - (float) (prop (clip, ids::fadeOut) * geo.pixelsPerSecond);
    if (p.y < top + 10.0f && std::abs (p.x - fiX) < 6.0f) return Mode::fadeIn;
    if (p.y < top + 10.0f && std::abs (p.x - foX) < 6.0f) return Mode::fadeOut;
    const float edge = juce::jmin (6.0f, (x1 - x0) * 0.25f);
    if (p.x < x0 + edge) return Mode::trimLeft;
    if (p.x > x1 - edge) return Mode::trimRight;
    return Mode::move;
}

static juce::ValueTree clipAt (ArrangeView& v, juce::Point<float> p)
{
    const int i = v.trackIndexAtY ((int) p.y);
    auto t = v.trackAtIndex (i);
    if (! t.isValid()) return {};
    const double time = v.geo.timeFor (p.x);
    for (int c = t.getNumChildren(); --c >= 0;)
    {
        auto clip = t.getChild (c);
        if (clip.hasType (ids::CLIP) && time >= prop (clip, ids::start) && time <= prop (clip, ids::start) + prop (clip, ids::length))
            return clip;
    }
    return {};
}

void TimelineCanvas::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (owner.automationMode() && owner.trackIndexAtY (e.y) < owner.numTracks()) { mode = Mode::none; autoMouseDown (e); return; }
    mode = Mode::none;
    dragged = false;
    const auto p = e.position;
    auto clip = clipAt (owner, p);
    const int ti = owner.trackIndexAtY ((int) p.y);
    if (auto t = owner.trackAtIndex (ti); t.isValid() && clip.isValid() && ! owner.session.isTrackSelected (t))
        owner.session.clickTrack (t, e.mods.isShiftDown() || e.mods.isCommandDown() ? juce::ModifierKeys (juce::ModifierKeys::commandModifier) : juce::ModifierKeys());

    if (e.mods.isPopupMenu())
    {
        if (clip.isValid())
        {
            if (! owner.selection.contains (clip)) owner.select (clip, false);
            showClipMenu (clip);
        }
        else
        {
            showLaneMenu (ti, owner.geo.timeFor (p.x));
        }
        owner.repaintAll();
        return;
    }

    if (owner.sliceMode())
    {
        // slice tool: cut the clip under the mouse (and the other selected clips under that time) at the click
        if (clip.isValid())
        {
            const double t = owner.geo.snap (owner.geo.timeFor (p.x), ! e.mods.isAltDown());
            juce::Array<juce::ValueTree> targets { clip };
            if (owner.selection.contains (clip))
                for (auto& c : owner.selection)
                    if (c != clip && t > prop (c, ids::start) + 0.001 && t < clipEnd (c) - 0.001) targets.add (c);
            if (! e.mods.isAltDown())   // grouped tracks are cut at the same time
                for (auto tr : owner.session.linkedTracks (clip.getParent()))
                    for (auto c : tr)
                        if (c.hasType (ids::CLIP) && t > prop (c, ids::start) + 0.001 && t < clipEnd (c) - 0.001) targets.addIfNotAlreadyThere (c);
            if (t > prop (clip, ids::start) + 0.001 && t < clipEnd (clip) - 0.001) owner.splitClipsAt (targets, t);
        }
        mode = Mode::none;
        return;
    }

    if (! clip.isValid())
    {
        // Empty space: a click selects the track and clears the clip selection; a drag draws a selection rectangle.
        mode = Mode::marquee;
        marqueeRect = {};
        return;
    }

    if (e.mods.isShiftDown() || e.mods.isCommandDown())
    {
        owner.select (clip, true);
        owner.repaintAll();
        if (! owner.selection.contains (clip)) return;
    }
    else if (! owner.selection.contains (clip))
    {
        owner.select (clip, false);
    }

    mode = modeAt (clip, p);
    anchor = clip;
    trackDelta = 0;
    originals.clear();
    auto& sel = owner.selection;
    if (mode == Mode::move)   // clips on grouped tracks come along
        for (int i = 0, n = sel.size(); i < n; ++i)
            for (auto& p : owner.groupPartners (sel[i])) sel.addIfNotAlreadyThere (p);
    juce::Array<juce::ValueTree> edited { clip };
    edited.addArray (owner.groupPartners (clip));   // trims and fades also apply to the grouped twins
    for (auto& c : (mode == Mode::move ? sel : edited))
        originals.push_back ({ c, prop (c, ids::start), prop (c, ids::length), prop (c, ids::offset), prop (c, ids::fadeIn),
                               prop (c, ids::fadeOut), owner.doc.tracks().indexOf (c.getParent()) });
    const char* names[] = { "", "Move clip", "Trim clip", "Trim clip", "Fade in", "Fade out", "" };
    owner.doc.undo().beginNewTransaction (names[(int) mode]);
    owner.repaintAll();
}

void TimelineCanvas::mouseDrag (const juce::MouseEvent& e)
{
    if (owner.automationMode() && autoLane.isValid()) { autoMouseDrag (e); return; }
    if (mode == Mode::marquee)
    {
        if (! dragged && e.getDistanceFromDragStart() < 4) return;
        dragged = true;
        marqueeRect = juce::Rectangle<int>::leftTopRightBottom (juce::jmin (e.getMouseDownX(), e.x), juce::jmin (e.getMouseDownY(), e.y),
                                                                juce::jmax (e.getMouseDownX(), e.x), juce::jmax (e.getMouseDownY(), e.y));
        auto& geo = owner.geo;
        const double t0 = geo.timeFor ((float) marqueeRect.getX()), t1 = geo.timeFor ((float) marqueeRect.getRight());
        const int first = owner.trackIndexAtY (marqueeRect.getY());
        const int last = juce::jmin (owner.numTracks() - 1, owner.trackIndexAtY (marqueeRect.getBottom()));
        if (! e.mods.isShiftDown()) owner.clearSelection();
        juce::Array<juce::ValueTree> tracksHit;
        for (int i = first; i <= last; ++i)
        {
            auto t = owner.trackAtIndex (i);
            tracksHit.add (t);
            for (auto c : t)
                if (c.hasType (ids::CLIP) && prop (c, ids::start) < t1 && clipEnd (c) > t0)
                    owner.selection.addIfNotAlreadyThere (c);
        }
        if (! tracksHit.isEmpty())
            owner.session.setSelectedTracks (tracksHit);
        owner.repaintAll();
        return;
    }
    if (mode == Mode::none || originals.empty()) return;
    if (! dragged && e.getDistanceFromDragStart() < 3) return;
    dragged = true;
    auto& geo = owner.geo;
    auto& doc = owner.doc;
    const bool snapOn = ! e.mods.isAltDown();
    const double dt = geo.timeFor ((float) e.x) - geo.timeFor ((float) e.getMouseDownX());
    const double minLen = 0.01;

    auto findOrig = [this] (const juce::ValueTree& c) -> Orig* {
        for (auto& o : originals) if (o.clip == c) return &o;
        return nullptr;
    };
    auto* ao = findOrig (anchor);
    if (ao == nullptr) return;
    auto audio = owner.session.audioFor (anchor);

    switch (mode)
    {
        case Mode::move:
        {
            const double newStart = geo.snap (ao->start + dt, snapOn);
            const double delta = newStart - ao->start;
            const int dTrack = owner.trackIndexAtY (e.y) - owner.trackIndexAtY (e.getMouseDownY());
            bool tracksOk = true;
            for (auto& o : originals)
                tracksOk = tracksOk && juce::isPositiveAndBelow (o.track + dTrack, owner.numTracks())
                           && acceptsClip (owner.trackAtIndex (o.track + dTrack), o.clip);   // audio / MIDI clips on their own kind of track
            for (auto& o : originals)
            {
                doc.set (o.clip, ids::start, juce::jmax (0.0, o.start + delta));
                if (tracksOk && dTrack != trackDelta)
                {
                    auto target = owner.trackAtIndex (o.track + dTrack);
                    auto parent = o.clip.getParent();
                    if (target.isValid() && parent != target)
                    {
                        parent.removeChild (o.clip, &doc.undo());
                        target.appendChild (o.clip, &doc.undo());
                    }
                }
            }
            if (tracksOk) trackDelta = dTrack;
            break;
        }
        case Mode::trimLeft:
        {
            // the anchor follows the mouse (snapped); grouped clips move their edge by the same amount
            const double shift = geo.snap (ao->start + dt, snapOn) - ao->start;
            for (auto& o : originals)
            {
                const double end = o.start + o.length;
                const double ns = juce::jlimit (juce::jmax (0.0, o.start - o.offset), end - minLen, o.start + shift);
                const double nl = end - ns;
                doc.set (o.clip, ids::start, ns);
                doc.set (o.clip, ids::offset, o.offset + (ns - o.start));
                doc.set (o.clip, ids::length, nl);
                doc.set (o.clip, ids::fadeIn, juce::jmin (o.fadeIn, nl * 0.5));
                doc.set (o.clip, ids::fadeOut, juce::jmin (o.fadeOut, nl * 0.5));
            }
            break;
        }
        case Mode::trimRight:
        {
            const double shift = geo.snap (ao->start + ao->length + dt, snapOn) - (ao->start + ao->length);
            for (auto& o : originals)
            {
                const auto a = o.clip == anchor ? audio : owner.session.audioFor (o.clip);
                const double len = a != nullptr ? a->lengthSeconds() : 1.0e9;
                const double ne = juce::jlimit (o.start + minLen, o.start + (len - o.offset), o.start + o.length + shift);
                const double nl = ne - o.start;
                doc.set (o.clip, ids::length, nl);
                doc.set (o.clip, ids::fadeIn, juce::jmin (o.fadeIn, nl * 0.5));
                doc.set (o.clip, ids::fadeOut, juce::jmin (o.fadeOut, nl * 0.5));
            }
            break;
        }
        case Mode::fadeIn:
            for (auto& o : originals)
                doc.set (o.clip, ids::fadeIn, juce::jlimit (0.0, o.length - o.fadeOut, o.fadeIn + dt));
            break;
        case Mode::fadeOut:
            for (auto& o : originals)
                doc.set (o.clip, ids::fadeOut, juce::jlimit (0.0, o.length - o.fadeIn, o.fadeOut - dt));
            break;
        case Mode::none:
        case Mode::marquee: break;
    }
    owner.repaintAll();
}

void TimelineCanvas::mouseUp (const juce::MouseEvent& e)
{
    if (autoLane.isValid()) { autoLane = {}; autoPoint = -1; autoTrack = -1; repaint(); return; }
    if (mode == Mode::marquee)
    {
        if (! dragged)
        {
            owner.clearSelection();
            if (auto t = owner.trackAtIndex (owner.trackIndexAtY (e.getMouseDownY())); t.isValid())
                owner.session.clickTrack (t, e.mods);
            // the playhead only moves from the ruler (bar numbers) at the top
        }
        mode = Mode::none;
        marqueeRect = {};
        owner.repaintAll();
        return;
    }
    if (mode == Mode::move && dragged)
    {
        // a moved clip goes on top of whatever it now overlaps
        for (auto& o : originals)
        {
            auto parent = o.clip.getParent();
            const int idx = parent.indexOf (o.clip);
            if (idx >= 0 && idx != parent.getNumChildren() - 1)
                parent.moveChild (idx, parent.getNumChildren() - 1, &owner.doc.undo());
        }
    }
    mode = Mode::none;
    originals.clear();
}

bool TimelineCanvas::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey && owner.sliceMode()) { owner.setSliceMode (false); return true; }
    return false;
}

void TimelineCanvas::mouseMove (const juce::MouseEvent& e)
{
    auto clip = clipAt (owner, e.position);
    if (owner.sliceMode())
    {
        sliceX = clip.isValid() ? owner.geo.xFor (owner.geo.snap (owner.geo.timeFor (e.position.x), ! e.mods.isAltDown())) : -1.0f;
        setMouseCursor (juce::MouseCursor::IBeamCursor);
        repaint();
        return;
    }
    if (sliceX >= 0.0f) { sliceX = -1.0f; repaint(); }
    auto newCursor = juce::MouseCursor::NormalCursor;
    if (clip.isValid())
    {
        switch (modeAt (clip, e.position))
        {
            case Mode::trimLeft:
            case Mode::trimRight: newCursor = juce::MouseCursor::LeftRightResizeCursor; break;
            case Mode::fadeIn:
            case Mode::fadeOut: newCursor = juce::MouseCursor::CrosshairCursor; break;
            case Mode::move: newCursor = juce::MouseCursor::DraggingHandCursor; break;
            case Mode::none:
            case Mode::marquee: break;
        }
    }
    setMouseCursor (newCursor);
}

juce::Rectangle<float> TimelineCanvas::laneRect (int i) const
{
    const auto& geo = owner.geo;
    return { 0.0f, (float) (i * geo.trackHeight - geo.scrollY) + 6.0f, (float) getWidth(), (float) geo.trackHeight - 13.0f };
}

juce::Point<float> TimelineCanvas::pointPos (const juce::String& prm, const automation::Point& p, juce::Rectangle<float> row) const
{
    return { owner.geo.xFor (p.time), row.getBottom() - automation::toNormal (prm, p.value) * row.getHeight() };
}

int TimelineCanvas::pointAt (juce::Point<float> at, const std::vector<automation::Point>& pts, const juce::String& prm, juce::Rectangle<float> row) const
{
    for (int k = 0; k < (int) pts.size(); ++k)
        if (pointPos (prm, pts[(size_t) k], row).getDistanceFrom (at) < 7.0f) return k;
    return -1;
}

void TimelineCanvas::drawAutomation (juce::Graphics& g)
{
    const int n = owner.numTracks();
    for (int i = 0; i < n; ++i)
    {
        const auto row = laneRect (i);
        if (row.getBottom() < 0.0f || row.getY() > (float) getHeight()) continue;
        auto t = owner.trackAtIndex (i);
        const auto prm = owner.laneParamFor (t);
        // dim the clips: the lane is what is being edited
        g.setColour (palette::bg.withAlpha (0.55f));
        g.fillRect (row.expanded (0.0f, 3.0f));
        const auto colour = palette::warn;
        const auto pts = automation::pointsOf (automation::lane (t, prm, false, nullptr));
        g.setFont (uiFont (10.5f, true));
        g.setColour (colour.withAlpha (0.8f));
        g.drawText (owner.laneLabel (t, prm), row.reduced (6.0f, 2.0f).toNearestInt(), juce::Justification::topLeft, true);
        if (pts.empty())
        {
            const float y = row.getBottom() - automation::toNormal (prm, automation::defaultValue (prm, t)) * row.getHeight();
            const float dashes[] = { 5.0f, 4.0f };
            g.setColour (colour.withAlpha (0.45f));
            g.drawDashedLine ({ 0.0f, y, (float) getWidth(), y }, dashes, 2, 1.5f);
            continue;
        }
        juce::Path p;
        const auto first = pointPos (prm, pts.front(), row);
        p.startNewSubPath (0.0f, first.y);
        for (auto& pt : pts) p.lineTo (pointPos (prm, pt, row));
        p.lineTo ((float) getWidth(), pointPos (prm, pts.back(), row).y);
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (2.0f));
        for (int k = 0; k < (int) pts.size(); ++k)
        {
            const auto c = pointPos (prm, pts[(size_t) k], row);
            if (c.x < -6.0f || c.x > (float) getWidth() + 6.0f) continue;
            const bool hot = i == autoTrack && k == autoPoint;
            g.setColour (hot ? juce::Colours::white : colour);
            g.fillEllipse (c.x - 4.5f, c.y - 4.5f, 9.0f, 9.0f);
            if (hot)
            {
                g.setFont (uiFont (11.0f, true));
                g.drawText (owner.laneValueText (t, prm, pts[(size_t) k].value), juce::Rectangle<float> (c.x + 8.0f, c.y - 16.0f, 120.0f, 14.0f),
                            juce::Justification::centredLeft, false);
            }
        }
    }
}

bool TimelineCanvas::autoMouseDown (const juce::MouseEvent& e)
{
    const int ti = owner.trackIndexAtY (e.y);
    auto t = owner.trackAtIndex (ti);
    autoTrack = -1;
    autoPoint = -1;
    if (! t.isValid()) return true;
    const auto prm = owner.laneParamFor (t);
    const auto row = laneRect (ti);
    auto existing = automation::lane (t, prm, false, nullptr);
    auto pts = automation::pointsOf (existing);
    const int hit = pointAt (e.position, pts, prm, row);
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addItem (1, "Delete point", hit >= 0);
        m.addItem (2, "Clear this lane", ! pts.empty());
        juce::Component::SafePointer<TimelineCanvas> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, t, prm, hit] (int r) {
            if (safe == nullptr || r == 0) return;
            auto l = automation::lane (t, prm, false, nullptr);
            if (! l.isValid()) return;
            auto p2 = automation::pointsOf (l);
            safe->owner.doc.undo().beginNewTransaction ("Automation");
            if (r == 1 && juce::isPositiveAndBelow (hit, (int) p2.size())) p2.erase (p2.begin() + hit);
            if (r == 2) p2.clear();
            automation::setPoints (l, p2, &safe->owner.doc.undo());
            safe->repaint();
        });
        return true;
    }
    owner.doc.undo().beginNewTransaction ("Automation");
    autoLane = automation::lane (t, prm, true, &owner.doc.undo());
    autoTrack = ti;
    if (hit >= 0) autoPoint = hit;
    else
    {
        automation::Point np;
        np.time = owner.geo.snap (owner.geo.timeFor (e.position.x), ! e.mods.isAltDown());
        np.value = automation::fromNormal (prm, (row.getBottom() - e.position.y) / row.getHeight());
        if (pts.empty())   // the first point: hold the current value up to here
        {
            const float v0 = automation::defaultValue (prm, t);
            if (np.time > 0.001) pts.push_back ({ 0.0, v0 });
        }
        pts.push_back (np);
        std::stable_sort (pts.begin(), pts.end(), [] (const automation::Point& a, const automation::Point& b) { return a.time < b.time; });
        for (int k = 0; k < (int) pts.size(); ++k)
            if (pts[(size_t) k].time == np.time && pts[(size_t) k].value == np.value) autoPoint = k;
        automation::setPoints (autoLane, pts, &owner.doc.undo());
    }
    autoPts = automation::pointsOf (autoLane);
    repaint();
    return true;
}

void TimelineCanvas::autoMouseDrag (const juce::MouseEvent& e)
{
    if (! autoLane.isValid() || ! juce::isPositiveAndBelow (autoPoint, (int) autoPts.size())) return;
    const auto t = owner.trackAtIndex (autoTrack);
    const auto prm = owner.laneParamFor (t);
    const auto row = laneRect (autoTrack);
    auto& p = autoPts[(size_t) autoPoint];
    const double lo = autoPoint > 0 ? autoPts[(size_t) autoPoint - 1].time : 0.0;
    const double hi = autoPoint + 1 < (int) autoPts.size() ? autoPts[(size_t) autoPoint + 1].time : 1.0e9;
    p.time = juce::jlimit (lo, hi, owner.geo.snap (owner.geo.timeFor (e.position.x), ! e.mods.isAltDown()));
    p.value = automation::fromNormal (prm, (row.getBottom() - e.position.y) / row.getHeight());
    automation::setPoints (autoLane, autoPts, &owner.doc.undo());
    repaint();
}

void TimelineCanvas::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! owner.automationMode() && ! owner.sliceMode())
        if (auto c = clipAt (owner, e.position); midiclip::isMidiClip (c)) { owner.openMidiEditor (c); return; }
    if (owner.automationMode())
    {
        const int ti = owner.trackIndexAtY (e.y);
        auto t = owner.trackAtIndex (ti);
        if (! t.isValid()) return;
        const auto prm = owner.laneParamFor (t);
        auto l = automation::lane (t, prm, false, nullptr);
        auto pts = automation::pointsOf (l);
        const int hit = pointAt (e.position, pts, prm, laneRect (ti));
        if (hit < 0) return;
        // type in the point's value
        auto* w = new juce::AlertWindow ("Automation point", owner.laneLabel (t, prm) + " at " + juce::String (pts[(size_t) hit].time, 2)
                                             + " s.  Type the value (" + (prm == automation::volumeParam ? juce::String ("dB, e.g. -6")
                                                                          : prm == automation::panParam ? juce::String ("e.g. C, 30 L, 100 R")
                                                                          : juce::String ("as the plugin shows it")) + ").",
                                         juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("v", owner.laneValueText (t, prm, pts[(size_t) hit].value));
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Delete point", 2);
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<TimelineCanvas> safe (this);
        const double when = pts[(size_t) hit].time;
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, t, prm, when] (int r) {
            if (safe == nullptr || r == 0) return;
            auto lane = automation::lane (t, prm, false, nullptr);
            auto p2 = automation::pointsOf (lane);
            int k = -1;
            for (int i = 0; i < (int) p2.size(); ++i) if (std::abs (p2[(size_t) i].time - when) < 1.0e-6) k = i;
            if (k < 0) return;
            auto& o = safe->owner;
            if (r == 2)
            {
                o.doc.undo().beginNewTransaction ("Delete automation point");
                p2.erase (p2.begin() + k);
            }
            else
            {
                float v = 0.0f;
                if (! o.laneValueFromText (t, prm, w->getTextEditorContents ("v"), v)) return;
                o.doc.undo().beginNewTransaction ("Automation value");
                p2[(size_t) k].value = v;
            }
            automation::setPoints (lane, p2, &o.doc.undo());
            safe->repaint();
        }), true);
        return;
    }
    if (owner.trackIndexAtY (e.y) >= owner.numTracks())
        owner.addTrack();
}

void TimelineCanvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    auto& geo = owner.geo;
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
        owner.zoom (w.deltaY > 0 ? 1.25 : 0.8, (float) e.x);
    else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
        geo.scrollX = juce::jmax (0.0, geo.scrollX - d * 300.0 / geo.pixelsPerSecond);
    }
    else
        geo.scrollY = juce::jmax (0, geo.scrollY - juce::roundToInt (w.deltaY * 200.0f));
    owner.updateScrollBars();
    owner.repaintAll();
}

bool TimelineCanvas::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3;mid;midi"))
            return true;
    return false;
}

void TimelineCanvas::filesDropped (const juce::StringArray& files, int x, int y)
{
    dropX = -1;
    juce::Array<juce::File> list;
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3;mid;midi"))
            list.add (juce::File (f));
    auto track = owner.trackAtIndex (owner.trackIndexAtY (y));
    const auto r = owner.session.importFiles (list, track, owner.geo.snap (owner.geo.timeFor ((float) x)));
    if (r.failed())
        owner.showError ("Import problem", r.getErrorMessage());
    owner.repaintAll();
}

void TimelineCanvas::showClipMenu (juce::ValueTree clip)
{
    const double ph = owner.playhead();
    const bool canSplit = ph > prop (clip, ids::start) && ph < clipEnd (clip);
    juce::PopupMenu m;
    m.addItem (1, "Split at playhead (S)", canSplit);
    m.addItem (2, "Duplicate (Ctrl+D)");
    m.addItem (3, "Delete (Del)");
    m.addSeparator();
    m.addItem (4, (bool) clip[ids::muted] ? "Unmute clip" : "Mute clip");
    m.addItem (5, "Normalize clip gain");
    m.addItem (6, "Reset clip gain", ! juce::exactlyEqual (prop (clip, ids::gainDb), 0.0));
    m.addItem (7, "Rename...");
    if (midiclip::isMidiClip (clip)) m.addItem (8, "Edit MIDI...  (double-click)");

    // takes: other clips on this track overlapping this one
    juce::PopupMenu takes;
    juce::Array<juce::ValueTree> overlapping;
    auto parent = clip.getParent();
    for (auto c : parent)
        if (c.hasType (ids::CLIP) && prop (c, ids::start) < clipEnd (clip) && clipEnd (c) > prop (clip, ids::start))
            overlapping.add (c);
    for (int i = 0; i < overlapping.size(); ++i)
        takes.addItem (100 + i, overlapping[i][ids::name].toString(), true, overlapping[i] == parent.getChild (parent.getNumChildren() - 1)
                                                                             || overlapping[i] == clip);
    m.addSubMenu ("Takes (bring to top)", takes, overlapping.size() > 1);

    juce::Component::SafePointer<TimelineCanvas> safe (this);
    m.showMenuAsync ({}, [safe, clip, overlapping] (int r) mutable {
        if (safe == nullptr || r == 0) return;
        auto& o = safe->owner;
        auto& um = o.doc.undo();
        if (r == 1) o.splitAtPlayhead();
        else if (r == 2) o.duplicateSelection();
        else if (r == 3) o.deleteSelection();
        else if (r == 4) { um.beginNewTransaction ("Mute clip"); o.doc.set (clip, ids::muted, ! (bool) clip[ids::muted]); }
        else if (r == 8) o.openMidiEditor (clip);
        else if (r == 5)
        {
            if (auto a = o.session.audioFor (clip))
            {
                const float pk = a->peakInRange (prop (clip, ids::offset), prop (clip, ids::offset) + prop (clip, ids::length));
                if (pk > 0.0f)
                {
                    um.beginNewTransaction ("Normalize clip");
                    o.doc.set (clip, ids::gainDb, juce::jlimit (-60.0, 24.0, -1.0 - (double) juce::Decibels::gainToDecibels (pk)));
                }
            }
        }
        else if (r == 6) { um.beginNewTransaction ("Reset clip gain"); o.doc.set (clip, ids::gainDb, 0.0); }
        else if (r == 7)
            o.renameDialog ("Rename clip", clip[ids::name].toString(), [&o, clip] (juce::String n) mutable {
                o.doc.undo().beginNewTransaction ("Rename clip");
                o.doc.set (clip, ids::name, n);
            });
        else if (r >= 100 && r - 100 < overlapping.size())
        {
            auto take = overlapping[r - 100];
            auto parent2 = take.getParent();
            um.beginNewTransaction ("Choose take");
            o.doc.set (take, ids::muted, false);
            parent2.moveChild (parent2.indexOf (take), parent2.getNumChildren() - 1, &um);
        }
        o.repaintAll();
    });
}

void TimelineCanvas::showLaneMenu (int trackIndex, double time)
{
    juce::PopupMenu m;
    m.addItem (1, "Paste at playhead (Ctrl+V)", true);
    m.addItem (2, "Import audio here...");
    m.addItem (3, "Add marker here");
    m.addSeparator();
    m.addItem (4, "Add track");
    m.addItem (5, "Add bus track");
    m.addItem (6, "Add FX track");
    juce::Component::SafePointer<TimelineCanvas> safe (this);
    m.showMenuAsync ({}, [safe, trackIndex, time] (int r) {
        if (safe == nullptr) return;
        auto& o = safe->owner;
        if (r == 1) o.paste();
        else if (r == 2) { o.locate (o.geo.snap (time)); o.importAudioDialog(); }
        else if (r == 3)
        {
            o.doc.undo().beginNewTransaction ("Add marker");
            o.doc.markers().appendChild (createMarker ("Marker " + juce::String (o.doc.markers().getNumChildren() + 1), o.geo.snap (time)),
                                         &o.doc.undo());
        }
        else if (r == 4) o.addTrack();
        else if (r == 5) o.session.addTrack ({}, -1, TrackKind::bus);
        else if (r == 6) o.session.addTrack ({}, -1, TrackKind::fx);
        juce::ignoreUnused (trackIndex);
    });
}

// =====================================================================================
// ArrangeView
// =====================================================================================

ArrangeView::ArrangeView (ProjectDocument& d, EngineHost& h, SessionController& s, juce::PropertiesFile& p)
    : doc (d), host (h), session (s), settings (p)
{
    addTrackButton.setTooltip ("Add an audio track (Ctrl+T); right-click for a bus or FX track");
    importButton.setTooltip ("Import WAV/AIFF/FLAC/OGG/MP3 onto the selected track at the playhead, or a MIDI file as instrument tracks (Ctrl+I)");
    zoomInButton.setTooltip ("Zoom in (Ctrl + mouse wheel)");
    zoomOutButton.setTooltip ("Zoom out");
    fitButton.setTooltip ("Zoom to fit the song");
    addTrackButton.onClick = [this] {
        if (juce::ModifierKeys::currentModifiers.isPopupMenu()) session.showAddTrackMenu (&addTrackButton);
        else session.showAddTrackMenu (&addTrackButton, session.primaryTrack().isValid() ? doc.tracks().indexOf (session.primaryTrack()) + 1 : -1);
    };
    importButton.onClick = [this] { importAudioDialog(); };
    zoomInButton.onClick = [this] { zoom (1.5); };
    zoomOutButton.onClick = [this] { zoom (1.0 / 1.5); };
    fitButton.onClick = [this] { zoomToFit(); };
    for (auto* b : { &addTrackButton, &importButton, &zoomInButton, &zoomOutButton, &fitButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    snapLabel.setText ("Snap", juce::dontSendNotification);
    styleLabel (snapLabel, 12.0f, palette::textDim, true);
    addAndMakeVisible (snapLabel);

    followButton.setClickingTogglesState (true);
    followButton.setToggleState (settings.getBoolValue ("studioFollow", true), juce::dontSendNotification);
    followButton.setTooltip ("Follow: the view scrolls along with the playhead while playing");
    followButton.onClick = [this] { settings.setValue ("studioFollow", followButton.getToggleState()); };
    sliceButton.setClickingTogglesState (true);
    sliceButton.setTooltip ("Slice: click a clip to cut it there (snaps to the grid; hold Alt for no snap). "
                            "Click Slice again (or press Esc) to go back to normal editing.");
    sliceButton.onClick = [this] { canvas.setMouseCursor (sliceMode() ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::NormalCursor); repaintAll(); };
    autoButton.setClickingTogglesState (true);
    autoButton.setTooltip ("Automation: draw volume, pan and plugin changes over time. Click a track's lane to add a point, "
                           "drag points, double-click one to type its value, right-click one to remove it. Pick the lane at the left of each track.");
    autoButton.onClick = [this] {
        if (automationMode() && sliceMode()) setSliceMode (false);
        for (auto* h : headers) h->refreshAutomation();
        repaintAll();
    };
    for (auto* b : { &followButton, &sliceButton, &autoButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    const std::pair<int, const char*> snaps[] = { { 1, "Off" }, { 2, "Bar" }, { 3, "1/2" }, { 4, "1/4 (beat)" }, { 5, "1/8" }, { 6, "1/16" } };
    for (auto& [id, n] : snaps) snapCombo.addItem (n, id);
    snapCombo.setSelectedId (settings.getIntValue ("arrangeSnap", 4), juce::dontSendNotification);
    snapCombo.onChange = [this] { settings.setValue ("arrangeSnap", snapCombo.getSelectedId()); syncGeometry(); };
    snapCombo.setTooltip ("Grid snapping (hold Alt while dragging to bypass)");
    addAndMakeVisible (snapCombo);

    geo.pixelsPerSecond = settings.getDoubleValue ("arrangeZoom", 60.0);

    addAndMakeVisible (ruler);
    addAndMakeVisible (canvas);
    addAndMakeVisible (headerHolder);
    hScroll.addListener (this);
    vScroll.addListener (this);
    hScroll.setAutoHide (false);
    vScroll.setAutoHide (false);
    addAndMakeVisible (hScroll);
    addAndMakeVisible (vScroll);

    session.selectionChanged.addChangeListener (this);
    rebind();
    startTimerHz (30);
}

ArrangeView::~ArrangeView()
{
    session.selectionChanged.removeChangeListener (this);
    root.removeListener (this);
    settings.setValue ("arrangeZoom", geo.pixelsPerSecond);
}

void ArrangeView::rebind()
{
    root.removeListener (this);
    root = doc.tree();
    root.addListener (this);
    selection.clear();
    syncGeometry();
    rebuildHeaders();
}

void ArrangeView::deviceChanged()
{
    for (auto* h : headers)
        h->refreshInputs();
}

void ArrangeView::syncGeometry()
{
    geo.bpm = doc.tempo();
    geo.tsNum = doc.tsNum();
    geo.tsDen = doc.tsDen();
    const double beatQ = 4.0 / geo.tsDen;
    switch (snapCombo.getSelectedId())
    {
        case 1: geo.snapBeats = 0.0; break;
        case 2: geo.snapBeats = geo.tsNum * beatQ; break;
        case 3: geo.snapBeats = 2.0; break;
        case 5: geo.snapBeats = 0.5; break;
        case 6: geo.snapBeats = 0.25; break;
        default: geo.snapBeats = 1.0; break;
    }
    repaintAll();
}

juce::Colour ArrangeView::trackColour (const juce::ValueTree& t) const
{
    return juce::Colour::fromString (t[ids::colour].toString().isNotEmpty() ? t[ids::colour].toString() : juce::String ("ff808080"));
}

int ArrangeView::trackIndexAtY (int y) const
{
    return juce::jmax (0, (y + geo.scrollY) / geo.trackHeight);
}

void ArrangeView::select (const juce::ValueTree& clip, bool add)
{
    if (! add) selection.clear();
    const auto partners = groupPartners (clip);
    if (add && selection.contains (clip))
    {
        selection.removeFirstMatchingValue (clip);
        for (auto& p : partners) selection.removeFirstMatchingValue (p);
    }
    else
    {
        selection.addIfNotAlreadyThere (clip);
        for (auto& p : partners) selection.addIfNotAlreadyThere (p);   // grouped tracks edit together
    }
}

juce::Array<juce::ValueTree> ArrangeView::groupPartners (const juce::ValueTree& clip) const
{
    juce::Array<juce::ValueTree> out;
    const auto track = clip.getParent();
    const auto grp = track[ids::group].toString();
    if (grp.isEmpty() || juce::ModifierKeys::currentModifiers.isAltDown()) return out;   // Alt: just this clip
    const double s = prop (clip, ids::start), e = clipEnd (clip);
    for (auto t : doc.tracks())
        if (t != track && t[ids::group].toString() == grp)
            for (auto c : t)
                if (c.hasType (ids::CLIP) && std::abs (prop (c, ids::start) - s) < 0.002 && std::abs (clipEnd (c) - e) < 0.002)
                    out.add (c);   // like Studio One: only the clips lined up with this one (same start and end)
    return out;
}

void ArrangeView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    for (auto* h : headers) h->repaint();
    canvas.repaint();
}

void ArrangeView::rebuildHeaders()
{
    headers.clear();
    auto tracks = doc.tracks();
    for (int i = 0; i < tracks.getNumChildren(); ++i)
    {
        auto* h = headers.add (new TrackHeader (*this, tracks.getChild (i), i));
        headerHolder.addAndMakeVisible (h);
    }
    selection.removeIf ([] (const juce::ValueTree& c) { return ! c.getParent().isValid() || ! c.getParent().getParent().isValid(); });
    layoutHeaders();
    updateScrollBars();
    repaint();
}

void ArrangeView::layoutHeaders()
{
    for (auto* h : headers)
        h->setBounds (0, h->index * geo.trackHeight - geo.scrollY, headerHolder.getWidth(), geo.trackHeight);
}

void ArrangeView::updateScrollBars()
{
    const double viewSec = canvas.getWidth() / geo.pixelsPerSecond;
    const double total = juce::jmax (songEnd (doc.tracks()) + 30.0, geo.scrollX + viewSec, 60.0);
    hScroll.setRangeLimits (0.0, total, juce::dontSendNotification);
    hScroll.setCurrentRange (geo.scrollX, viewSec, juce::dontSendNotification);
    const int contentH = numTracks() * geo.trackHeight + 80;
    vScroll.setRangeLimits (0.0, juce::jmax (contentH, canvas.getHeight()), juce::dontSendNotification);
    geo.scrollY = juce::jlimit (0, juce::jmax (0, contentH - canvas.getHeight()), geo.scrollY);
    vScroll.setCurrentRange (geo.scrollY, canvas.getHeight(), juce::dontSendNotification);
    layoutHeaders();
}

void ArrangeView::scrollBarMoved (juce::ScrollBar* bar, double start)
{
    if (bar == &hScroll) geo.scrollX = start;
    else geo.scrollY = (int) start;
    layoutHeaders();
    repaintAll();
}

void ArrangeView::repaintAll()
{
    canvas.repaint();
    ruler.repaint();
}

void ArrangeView::timerCallback()
{
    for (auto* h : headers)
        h->tickMeter();

    if (host.engine.isPlaying() && followButton.getToggleState())
    {
        const double ph = playhead();
        const double viewSec = canvas.getWidth() / geo.pixelsPerSecond;
        if (ph > geo.scrollX + viewSec * 0.92 || ph < geo.scrollX)
        {
            geo.scrollX = juce::jmax (0.0, ph - viewSec * 0.08);
            updateScrollBars();
        }
    }
    repaintAll();
}

void ArrangeView::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (parent.hasType (pids::TRACKS) || child.hasType (pids::TRACKS)) rebuildHeaders();
    else repaintAll();
}

void ArrangeView::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    selection.removeFirstMatchingValue (child);
    if (parent.hasType (pids::TRACKS)) rebuildHeaders();
    else repaintAll();
}

void ArrangeView::valueTreeChildOrderChanged (juce::ValueTree& parent, int, int)
{
    if (parent.hasType (pids::TRACKS)) rebuildHeaders();
    else repaintAll();
}

void ArrangeView::valueTreePropertyChanged (juce::ValueTree& node, const juce::Identifier&)
{
    if (node.hasType (pids::TRANSPORT)) syncGeometry();
    else repaintAll();
}

void ArrangeView::paint (juce::Graphics& g)
{
    g.fillAll (palette::bg);
    auto tb = getLocalBounds().removeFromTop (toolbarHeight);
    g.setColour (palette::panel);
    g.fillRect (tb);
    g.setColour (palette::outline);
    g.drawHorizontalLine (tb.getBottom() - 1, 0.0f, (float) getWidth());
    auto corner = juce::Rectangle<int> (0, toolbarHeight, headerWidth, rulerHeight);
    g.setColour (palette::panel);
    g.fillRect (corner);
    g.setColour (palette::textDim);
    g.setFont (uiFont (11.0f, true));
    g.drawText (juce::String (numTracks()) + (numTracks() == 1 ? " TRACK" : " TRACKS"), corner.reduced (12, 0), juce::Justification::centredLeft);
    g.setColour (palette::outline);
    g.drawVerticalLine (headerWidth - 1, (float) toolbarHeight, (float) getHeight());
}

void ArrangeView::resized()
{
    auto r = getLocalBounds();
    auto tb = r.removeFromTop (toolbarHeight).reduced (8, 5);
    addTrackButton.setBounds (tb.removeFromLeft (80));
    tb.removeFromLeft (6);
    importButton.setBounds (tb.removeFromLeft (70));
    tb.removeFromLeft (20);
    snapLabel.setBounds (tb.removeFromLeft (40));
    snapCombo.setBounds (tb.removeFromLeft (110));
    tb.removeFromLeft (20);
    sliceButton.setBounds (tb.removeFromLeft (64));
    tb.removeFromLeft (6);
    autoButton.setBounds (tb.removeFromLeft (96));
    tb.removeFromLeft (6);
    followButton.setBounds (tb.removeFromLeft (70));
    fitButton.setBounds (tb.removeFromRight (44));
    tb.removeFromRight (4);
    zoomInButton.setBounds (tb.removeFromRight (30));
    tb.removeFromRight (4);
    zoomOutButton.setBounds (tb.removeFromRight (30));

    auto rulerRow = r.removeFromTop (rulerHeight);
    rulerRow.removeFromLeft (headerWidth);
    ruler.setBounds (rulerRow.withTrimmedRight (12));
    auto bottom = r.removeFromBottom (12);
    hScroll.setBounds (bottom.withTrimmedLeft (headerWidth).withTrimmedRight (12));
    vScroll.setBounds (r.removeFromRight (12));
    headerHolder.setBounds (r.removeFromLeft (headerWidth));
    canvas.setBounds (r);
    updateScrollBars();
}

// ------------------------------------------------------------------ commands

void ArrangeView::showError (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle (title).withMessage (message).withButton ("OK").withAssociatedComponent (this),
                                  nullptr);
}

void ArrangeView::renameDialog (const juce::String& title, const juce::String& current, std::function<void (juce::String)> onDone)
{
    auto* w = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", current);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, onDone] (int r) {
        const auto n = w->getTextEditorContents ("name").trim();
        if (r == 1 && n.isNotEmpty()) onDone (n);
    }), true);
}

void ArrangeView::addTrack()
{
    const auto primary = session.primaryTrack();
    session.addTrack ({}, primary.isValid() ? doc.tracks().indexOf (primary) + 1 : -1);
}

juce::String ArrangeView::laneParamFor (const juce::ValueTree& track) const
{
    const auto p = track[automation::showLane].toString();
    return p.isNotEmpty() ? p : automation::volumeParam;
}

juce::String ArrangeView::laneLabel (const juce::ValueTree& track, const juce::String& prm) const
{
    if (prm == automation::volumeParam) return "Volume";
    if (prm == automation::panParam) return "Pan";
    int slot = -1, index = -1;
    if (! automation::parsePluginParam (prm, slot, index)) return prm;
    for (int i = 0; i < rig::numInserts (track); ++i)
    {
        const auto node = rig::insertNode (track, i);
        if ((int) node[rig::ids::slot] != slot) continue;
        juce::String pname = "parameter " + juce::String (index + 1);
        if (auto p = session.rigs().insertPlugin (track, i))
            if (juce::isPositiveAndBelow (index, p->getParameters().size())) pname = p->getParameters()[index]->getName (40);
        return node[rig::ids::name].toString() + ": " + pname;
    }
    return "Removed plugin";
}

void ArrangeView::openMidiEditor (juce::ValueTree clip)
{
    if (! midiclip::isMidiClip (clip)) return;
    const auto id = clip[ids::id].toString();
    auto& me = midiEditors[id];
    if (me.window == nullptr)
    {
        me.editor = std::make_unique<PianoRoll> (doc, host, clip);
        me.window = std::make_unique<ToolWindow> ("MIDI editor - " + clip.getParent()[ids::name].toString() + " / " + clip[ids::name].toString(),
                                                  *me.editor, 1000, 560, settings, "midiEditorWindow");
        me.window->setResizeLimits (520, 320, 4000, 3000);
        juce::Component::SafePointer<ArrangeView> safe (this);
        auto close = [safe, id] {
            juce::MessageManager::callAsync ([safe, id] { if (safe != nullptr) safe->midiEditors.erase (id); });
        };
        me.window->onClosed = close;
        me.editor->onClipGone = close;
    }
    me.window->showFront();
    me.editor->grabKeyboardFocus();
}

juce::AudioProcessorParameter* ArrangeView::laneParameter (const juce::ValueTree& track, const juce::String& prm) const
{
    int slot = -1, index = -1;
    if (! automation::parsePluginParam (prm, slot, index)) return nullptr;
    for (int i = 0; i < rig::numInserts (track); ++i)
        if ((int) rig::insertNode (track, i)[rig::ids::slot] == slot)
            if (auto p = session.rigs().insertPlugin (track, i))
                return juce::isPositiveAndBelow (index, p->getParameters().size()) ? p->getParameters()[index] : nullptr;
    return nullptr;
}

juce::String ArrangeView::laneValueText (const juce::ValueTree& track, const juce::String& prm, float v) const
{
    if (auto* p = laneParameter (track, prm))
    {
        const auto text = p->getText (v, 32).trim();
        const auto label = p->getLabel().trim();
        return label.isNotEmpty() && ! text.endsWith (label) ? text + " " + label : text;
    }
    return automation::valueText (prm, v);
}

bool ArrangeView::laneValueFromText (const juce::ValueTree& track, const juce::String& prm, const juce::String& textIn, float& out) const
{
    const auto text = textIn.trim().toLowerCase();
    if (text.isEmpty()) return false;
    if (prm == automation::volumeParam)
    {
        if (text.contains ("inf")) { out = -60.0f; return true; }
        out = juce::jlimit (-60.0f, 6.0f, text.retainCharacters ("-+.0123456789").getFloatValue());
        return true;
    }
    if (prm == automation::panParam)
    {
        if (text == "c" || text == "center" || text == "centre") { out = 0.0f; return true; }
        float amount = text.retainCharacters ("-+.0123456789").getFloatValue();
        if (text.containsChar ('l')) amount = -std::abs (amount);
        else if (text.containsChar ('r')) amount = std::abs (amount);
        if (text.retainCharacters ("0123456789").isEmpty()) amount = text.containsChar ('l') ? -100.0f : 100.0f;   // just "L" / "R"
        out = juce::jlimit (-1.0f, 1.0f, std::abs (amount) > 1.0f ? amount / 100.0f : amount);
        return true;
    }
    if (auto* p = laneParameter (track, prm))
    {
        out = juce::jlimit (0.0f, 1.0f, p->getValueForText (textIn.trim()));
        return true;
    }
    out = juce::jlimit (0.0f, 1.0f, text.retainCharacters ("-+.0123456789").getFloatValue() / 100.0f);   // percent
    return true;
}

void ArrangeView::showLaneChooser (juce::ValueTree track, juce::Component* target)
{
    juce::PopupMenu m;
    const auto cur = laneParamFor (track);
    auto has = [&track] (const juce::String& prm) { return automation::lane (track, prm, false, nullptr)[automation::points].toString().isNotEmpty(); };
    juce::StringArray params;
    auto add = [&] (juce::PopupMenu& menu, const juce::String& prm, const juce::String& label) {
        params.add (prm);
        menu.addItem (params.size(), label + (has (prm) ? "   *" : ""), true, prm == cur);
    };
    m.addSectionHeader ("Automation lane  (* = has points)");
    add (m, automation::volumeParam, "Volume");
    add (m, automation::panParam, "Pan");
    for (int i = 0; i < rig::numInserts (track); ++i)
    {
        const auto node = rig::insertNode (track, i);
        juce::PopupMenu sub;
        if (auto p = session.rigs().insertPlugin (track, i))
        {
            const auto& ps = p->getParameters();
            for (int k = 0; k < ps.size() && k < 400; ++k)
                if (ps[k]->isAutomatable()) add (sub, automation::pluginParam ((int) node[rig::ids::slot], k), ps[k]->getName (60));
        }
        else sub.addItem (-1, "(the plugin is not loaded)", false);
        m.addSubMenu (node[rig::ids::name].toString(), sub);
    }
    if (rig::numInserts (track) == 0) m.addItem (-1, "(add plugins to the track to automate their parameters)", false);
    m.addSeparator();
    m.addItem (-2, "Clear this lane", has (cur));
    juce::Component::SafePointer<ArrangeView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target).withMousePosition(), [safe, track, params, cur] (int r) {
        if (safe == nullptr) return;
        auto t = track;
        if (r == -2)
        {
            safe->doc.undo().beginNewTransaction ("Clear automation");
            if (auto l = automation::lane (t, cur, false, nullptr); l.isValid()) automation::setPoints (l, {}, &safe->doc.undo());
        }
        else if (r > 0 && r <= params.size()) t.setProperty (automation::showLane, params[r - 1], nullptr);
        for (auto* h : safe->headers) h->refreshAutomation();
        safe->repaintAll();
    });
}

void ArrangeView::importAudioDialog()
{
    chooser = std::make_unique<juce::FileChooser> ("Import audio", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.mid;*.midi");
    juce::Component::SafePointer<ArrangeView> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [safe] (const juce::FileChooser& fc) {
                              if (safe == nullptr || fc.getResults().isEmpty()) return;
                              const auto r = safe->session.importFiles (fc.getResults(), safe->session.primaryTrack(), safe->playhead());
                              if (r.failed()) safe->showError ("Import problem", r.getErrorMessage());
                          });
}

void ArrangeView::deleteSelection()
{
    if (selection.isEmpty()) return;
    doc.undo().beginNewTransaction ("Delete clips");
    for (auto& c : selection)
        if (auto p = c.getParent(); p.isValid())
            p.removeChild (c, &doc.undo());
    selection.clear();
    repaintAll();
}

void ArrangeView::splitAtPlayhead()
{
    const double t = playhead();
    juce::Array<juce::ValueTree> targets;
    for (auto& c : selection)
        if (t > prop (c, ids::start) + 0.001 && t < clipEnd (c) - 0.001) targets.add (c);
    if (targets.isEmpty())
        for (auto sel : session.selectedTracks)
            for (auto tr : session.linkedTracks (sel))
                for (auto c : tr)
                    if (c.hasType (ids::CLIP) && t > prop (c, ids::start) + 0.001 && t < clipEnd (c) - 0.001) targets.addIfNotAlreadyThere (c);
    for (int i = 0; i < targets.size(); ++i)   // grouped tracks are cut too
        for (auto& p : groupPartners (targets[i]))
            if (t > prop (p, ids::start) + 0.001 && t < clipEnd (p) - 0.001) targets.addIfNotAlreadyThere (p);
    splitClipsAt (targets, t);
}

void ArrangeView::splitClipsAt (const juce::Array<juce::ValueTree>& targets, double t)
{
    if (targets.isEmpty()) return;

    doc.undo().beginNewTransaction ("Split");
    for (auto c : targets)
    {
        const double s = prop (c, ids::start), len = prop (c, ids::length);
        auto right = c.createCopy();
        right.setProperty (ids::id, newId(), nullptr);
        right.setProperty (ids::start, t, nullptr);
        right.setProperty (ids::offset, prop (c, ids::offset) + (t - s), nullptr);
        right.setProperty (ids::length, s + len - t, nullptr);
        right.setProperty (ids::fadeIn, juce::jmin (defaultFade, (s + len - t) * 0.5), nullptr);
        right.setProperty (ids::fadeOut, juce::jmin (prop (c, ids::fadeOut), (s + len - t) * 0.5), nullptr);
        doc.set (c, ids::length, t - s);
        doc.set (c, ids::fadeOut, juce::jmin (defaultFade, (t - s) * 0.5));
        doc.set (c, ids::fadeIn, juce::jmin (prop (c, ids::fadeIn), (t - s) * 0.5));
        auto parent = c.getParent();
        parent.addChild (right, parent.indexOf (c) + 1, &doc.undo());
    }
    selection.clear();   // a click after a split picks just that piece (and its grouped twins)
    repaintAll();
}

void ArrangeView::duplicateSelection()
{
    if (selection.isEmpty()) return;
    double a = 1.0e12, b = 0.0;
    for (auto& c : selection) { a = juce::jmin (a, prop (c, ids::start)); b = juce::jmax (b, clipEnd (c)); }
    const double shift = b - a;
    doc.undo().beginNewTransaction ("Duplicate");
    juce::Array<juce::ValueTree> created;
    for (auto& c : selection)
    {
        auto copy = c.createCopy();
        copy.setProperty (ids::id, newId(), nullptr);
        copy.setProperty (ids::start, prop (c, ids::start) + shift, nullptr);
        c.getParent().appendChild (copy, &doc.undo());
        created.add (copy);
    }
    selection = created;
    repaintAll();
}

void ArrangeView::copySelection (bool cut)
{
    if (selection.isEmpty()) return;
    clipboard.clear();
    double a = 1.0e12;
    int firstTrack = 1 << 20;
    for (auto& c : selection)
    {
        a = juce::jmin (a, prop (c, ids::start));
        firstTrack = juce::jmin (firstTrack, doc.tracks().indexOf (c.getParent()));
    }
    for (auto& c : selection)
        clipboard.push_back ({ c.createCopy(), doc.tracks().indexOf (c.getParent()) - firstTrack, prop (c, ids::start) - a });
    if (cut)
    {
        doc.undo().beginNewTransaction ("Cut");
        for (auto& c : selection)
            c.getParent().removeChild (c, &doc.undo());
        selection.clear();
    }
    repaintAll();
}

void ArrangeView::paste()
{
    if (clipboard.empty()) return;
    const auto primary = session.primaryTrack();
    int base = primary.isValid() ? doc.tracks().indexOf (primary) : 0;
    if (numTracks() == 0) { session.addTrack(); base = 0; }
    doc.undo().beginNewTransaction ("Paste");
    selection.clear();
    const double at = playhead();
    for (auto& item : clipboard)
    {
        auto t = trackAtIndex (juce::jlimit (0, numTracks() - 1, base + item.trackOffset));
        if (! acceptsClip (t, item.clip)) continue;   // audio / MIDI clips only on their own kind of track
        auto c = item.clip.createCopy();
        c.setProperty (ids::id, newId(), nullptr);
        c.setProperty (ids::start, at + item.timeOffset, nullptr);
        t.appendChild (c, &doc.undo());
        selection.add (c);
    }
    repaintAll();
}

void ArrangeView::selectAll()
{
    selection.clear();
    for (auto t : doc.tracks())
        for (auto c : t)
            if (c.hasType (ids::CLIP)) selection.add (c);
    repaintAll();
}

void ArrangeView::setTrackDropIndex (int idx)
{
    if (dropIndex == idx) return;
    dropIndex = idx;
    repaint();
}

void ArrangeView::paintOverChildren (juce::Graphics& g)
{
    if (dropIndex < 0) return;
    const int y = headerHolder.getY() + dropIndex * geo.trackHeight - geo.scrollY;
    g.setColour (palette::accent);
    g.fillRect (0, y - 2, getWidth(), 3);
}

void ArrangeView::moveTracks (juce::Array<juce::ValueTree> moving, int insertIndex)
{
    auto tracks = doc.tracks();
    std::sort (moving.begin(), moving.end(), [&] (const juce::ValueTree& a, const juce::ValueTree& b) {
        return tracks.indexOf (a) < tracks.indexOf (b);
    });
    // desired order: the untouched tracks with the moved block inserted at the drop point
    juce::Array<juce::ValueTree> order;
    int insertAt = insertIndex;
    for (int i = 0; i < tracks.getNumChildren(); ++i)
    {
        auto t = tracks.getChild (i);
        if (moving.contains (t)) { if (i < insertIndex) --insertAt; continue; }
        order.add (t);
    }
    for (int i = 0; i < moving.size(); ++i)
        order.insert (insertAt + i, moving[i]);

    bool changed = false;
    for (int i = 0; i < order.size(); ++i)
        changed = changed || tracks.indexOf (order[i]) != i;
    if (! changed) return;

    doc.undo().beginNewTransaction (moving.size() > 1 ? "Move tracks" : "Move track");
    for (int i = 0; i < order.size(); ++i)
    {
        const int from = tracks.indexOf (order[i]);
        if (from != i) tracks.moveChild (from, i, &doc.undo());
    }
}

void ArrangeView::addMarkerAtPlayhead()
{
    doc.undo().beginNewTransaction ("Add marker");
    doc.markers().appendChild (createMarker ("Marker " + juce::String (doc.markers().getNumChildren() + 1), playhead()), &doc.undo());
}

void ArrangeView::zoom (double factor, float aroundX)
{
    const float x = aroundX >= 0.0f ? aroundX : geo.xFor (playhead());
    const double tAt = geo.timeFor (x);
    geo.pixelsPerSecond = juce::jlimit (2.0, 2000.0, geo.pixelsPerSecond * factor);
    geo.scrollX = juce::jmax (0.0, tAt - x / geo.pixelsPerSecond);
    updateScrollBars();
    repaintAll();
}

void ArrangeView::zoomToFit()
{
    const double end = juce::jmax (songEnd (doc.tracks()), geo.barSec() * 4.0);
    geo.pixelsPerSecond = juce::jlimit (2.0, 2000.0, (canvas.getWidth() - 40) / end);
    geo.scrollX = 0.0;
    updateScrollBars();
    repaintAll();
}
} // namespace ff
