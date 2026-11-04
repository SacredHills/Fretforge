#include "MixerPage.h"
#include "core/Timeline.h"

namespace ff
{
using namespace timeline;

static void setupFader (juce::Slider& f)
{
    f.setSliderStyle (juce::Slider::LinearVertical);
    f.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 20);
    f.setRange (-60.0, 6.0, 0.1);
    f.setSkewFactorFromMidPoint (-12.0);
    f.setDoubleClickReturnValue (true, 0.0);
    f.textFromValueFunction = [] (double v) { return v <= -59.95 ? juce::String ("-inf dB") : juce::String (v, 1) + " dB"; };
    f.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("-0123456789.").getDoubleValue(); };
}

// ------------------------------------------------------------------ strip

ChannelStrip::ChannelStrip (ProjectDocument& d, EngineHost& h, SessionController& sc, juce::ValueTree t, int s)
    : doc (d), host (h), session (sc), track (t), slot (s)
{
    setupFader (fader);
    fader.onDragStart = [this] { doc.undo().beginNewTransaction ("Track volume"); };
    fader.onValueChange = [this] {
        if (updating) return;
        const double before = juce::jmax (-60.0, (double) track[ids::gainDb]);
        const double now = fader.getValue();
        doc.set (track, ids::gainDb, now <= -59.95 ? -96.0 : now);
        for (auto other : session.tracksForEdit (track)) // grouped move for a multi-selection
            if (other != track)
            {
                const double v = juce::jlimit (-60.0, 6.0, juce::jmax (-60.0, (double) other[ids::gainDb]) + (now - before));
                doc.set (other, ids::gainDb, v <= -59.95 ? -96.0 : v);
            }
    };
    addAndMakeVisible (fader);

    pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    pan.setRange (-1.0, 1.0, 0.01);
    pan.setDoubleClickReturnValue (true, 0.0);
    pan.setPopupDisplayEnabled (true, true, nullptr);
    pan.onDragStart = [this] { doc.undo().beginNewTransaction ("Track pan"); };
    pan.onValueChange = [this] {
        if (updating) return;
        const double delta = pan.getValue() - (double) track[ids::pan];
        doc.set (track, ids::pan, pan.getValue());
        for (auto other : session.tracksForEdit (track))
            if (other != track)
                doc.set (other, ids::pan, juce::jlimit (-1.0, 1.0, (double) other[ids::pan] + delta));
    };
    addAndMakeVisible (pan);

    auto setup = [this] (juce::TextButton& b, juce::Colour c, const juce::Identifier& id) {
        b.setColour (juce::TextButton::buttonOnColourId, c);
        b.setWantsKeyboardFocus (false);
        b.onClick = [this, &id] { session.setOnTracks (track, id, ! (bool) track[id], false); };
        addAndMakeVisible (b);
    };
    setup (mute, palette::accent, ids::mute);
    setup (solo, palette::warn, ids::solo);
    setup (arm, palette::bad, ids::armed);

    ampButton.setColour (juce::TextButton::buttonOnColourId, palette::accent);
    ampButton.setWantsKeyboardFocus (false);
    ampButton.setTooltip ("Neural Amp Modeler amp on this track: click to add / open it");
    ampButton.onClick = [this] { session.addTrackRig (track); };
    addAndMakeVisible (ampButton);

    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setFont (uiFont (12.5f, true));
    nameLabel.setColour (juce::Label::textColourId, palette::text);
    nameLabel.setInterceptsMouseClicks (false, false);   // clicks select the strip; double-click renames
    nameLabel.setTooltip ("Double-click to rename");
    nameLabel.onTextChange = [this] {
        const auto n = nameLabel.getText().trim();
        if (n.isEmpty() || n == track[ids::name].toString()) { refresh(); return; }
        doc.undo().beginNewTransaction ("Rename track");
        doc.set (track, ids::name, n);
    };
    addAndMakeVisible (nameLabel);
    insertList.setTooltip ("Plugins on this track (after its NAM amp, before the fader). Click a plugin to open it, right-click for more.");
    addAndMakeVisible (insertList);
    session.rigs().statusChanged.addChangeListener (this);
    track.addListener (this);
    refresh();
}

ChannelStrip::~ChannelStrip()
{
    session.rigs().statusChanged.removeChangeListener (this);
    track.removeListener (this);
}

int InsertList::rowsWanted() const
{
    const int n = rig::numInserts (track);
    return n + (n < rig::maxInserts && session.plugins != nullptr ? 1 : 0);
}

void InsertList::paint (juce::Graphics& g)
{
    const int n = rig::numInserts (track);
    for (int i = 0; i < rowsWanted(); ++i)
    {
        auto r = juce::Rectangle<int> (0, i * rowHeight, getWidth(), rowHeight - 2).toFloat();
        if (i < n)
        {
            const auto node = rig::insertNode (track, i);
            const auto st = session.rigs().insertStatus (track, i);
            const bool bypassed = (bool) node[rig::ids::bypass];
            g.setColour (st.error.isNotEmpty() ? palette::bad.withAlpha (0.25f) : palette::info.withAlpha (bypassed ? 0.08f : 0.22f));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (bypassed ? palette::textDim : palette::text);
            g.setFont (uiFont (11.0f, ! bypassed));
            g.drawText ((st.loading ? "... " : juce::String()) + node[rig::ids::name].toString(), r.reduced (5.0f, 0.0f), juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour (palette::outline);
            g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
            g.setColour (palette::textDim);
            g.setFont (uiFont (11.0f));
            g.drawText ("+ Plugin", r, juce::Justification::centred);
        }
    }
}

void InsertList::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked()) return;
    const int row = e.y / rowHeight, n = rig::numInserts (track);
    if (row < n)
    {
        if (e.mods.isPopupMenu()) session.showInsertMenu (track, row, this);
        else session.openInsert (track, row);
    }
    else if (row < rowsWanted()) session.showAddInsertMenu (track, this);
}

void ChannelStrip::refresh()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    fader.setValue (juce::jmax (-60.0, (double) track[ids::gainDb]), juce::dontSendNotification);
    pan.setValue ((double) track[ids::pan], juce::dontSendNotification);
    mute.setToggleState ((bool) track[ids::mute], juce::dontSendNotification);
    solo.setToggleState ((bool) track[ids::solo], juce::dontSendNotification);
    arm.setToggleState ((bool) track[ids::armed], juce::dontSendNotification);
    ampButton.setToggleState (session.trackHasRig (track), juce::dontSendNotification);
    if (! nameLabel.isBeingEdited()) nameLabel.setText (track[ids::name].toString(), juce::dontSendNotification);
    repaint();
}

void ChannelStrip::tick()
{
    auto& m = host.engine.trackMeters[(size_t) juce::jlimit (0, maxTracks - 1, slot)];
    m.uiTake();
    const float db = gainToDb (m.uiPeak);
    level = db > level ? db : juce::jmax (db, level - 1.6f);
    if (db >= hold) { hold = db; holdTicks = 45; }
    else if (--holdTicks <= 0) hold = juce::jmax (-100.0f, hold - 1.5f);
    repaint();
}

void ChannelStrip::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (! session.isTrackSelected (track)) session.clickTrack (track, {});
        showMenu();
        return;
    }
    session.clickTrack (track, e.mods); // click the strip (name area / background) to select; Shift/Ctrl extend
}

void ChannelStrip::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.y < 34) nameLabel.showEditor();
}

void ChannelStrip::showMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Rename...");
    m.addSeparator();
    if (session.trackHasRig (track)) { m.addItem (10, "Open NAM amp..."); m.addItem (11, "Remove NAM amp"); }
    else m.addItem (10, "Add NAM amp...");
    m.addItem (12, "Add plugin...", session.plugins != nullptr && rig::numInserts (track) < rig::maxInserts);
    m.addSeparator();
    m.addItem (4, "Duplicate track");
    m.addItem (5, "Delete track");
    juce::Component::SafePointer<ChannelStrip> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r) {
        if (safe == nullptr || r == 0) return;
        auto t = safe->track;
        auto& sc = safe->session;
        if (r == 1) safe->nameLabel.showEditor();
        else if (r == 10) sc.addTrackRig (t);
        else if (r == 12) sc.showAddInsertMenu (t, safe.getComponent());
        else if (r == 11) sc.removeTrackRig (t);
        else if (r == 4) sc.duplicateTrack (t);
        else if (r == 5) sc.deleteTrack (t);   // destroys this strip
    });
}

void ChannelStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (3, 0);
    const bool selected = session.isTrackSelected (track);
    g.setColour (selected ? palette::panelHi : palette::panel);
    g.fillRoundedRectangle (r.toFloat(), 5.0f);
    if (selected)
    {
        g.setColour (palette::accent.withAlpha (0.8f));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 5.0f, 1.0f);
    }
    const auto colour = juce::Colour::fromString (track[ids::colour].toString());
    g.setColour (colour);
    g.fillRoundedRectangle (r.removeFromTop (6).toFloat(), 3.0f);

    auto meter = getLocalBounds().reduced (3, 0).withTrimmedTop (fader.getY()).withTrimmedBottom (34).removeFromRight (16).reduced (3, 0);
    g.setColour (palette::well);
    g.fillRect (meter);
    auto norm = [] (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
    g.setColour (level > -0.5f ? palette::bad : (level > -9.0f ? palette::warn : palette::good));
    g.fillRect (meter.withTop (meter.getBottom() - juce::roundToInt (norm (level) * meter.getHeight())));
    if (hold > -60.0f)
    {
        g.setColour (palette::text);
        g.fillRect (meter.getX(), meter.getBottom() - juce::roundToInt (norm (hold) * meter.getHeight()), meter.getWidth(), 1);
    }
    g.setColour (palette::textDim);
    g.setFont (uiFont (10.0f));
    g.drawText ("PAN", getLocalBounds().withTrimmedTop (30).withHeight (14), juce::Justification::centred);
}

void ChannelStrip::resized()
{
    auto r = getLocalBounds().reduced (8, 0).withTrimmedTop (44);
    pan.setBounds (r.removeFromTop (40).withSizeKeepingCentre (40, 40));
    r.removeFromTop (6);
    auto buttons = r.removeFromTop (24);
    const int w = buttons.getWidth() / 3;
    arm.setBounds (buttons.removeFromLeft (w).reduced (1));
    mute.setBounds (buttons.removeFromLeft (w).reduced (1));
    solo.setBounds (buttons.reduced (1));
    ampButton.setBounds (r.removeFromTop (30).reduced (1, 4));
    insertList.setBounds (r.removeFromTop (insertList.rowsWanted() * InsertList::rowHeight).withTrimmedRight (0));
    r.removeFromTop (insertList.rowsWanted() > 0 ? 6 : 0);
    nameLabel.setBounds (getLocalBounds().reduced (5, 0).withTrimmedTop (6).withHeight (24));
    r.removeFromBottom (6);
    fader.setBounds (r.withTrimmedRight (16));
}

// ------------------------------------------------------------------ page

MixerPage::MixerPage (EngineHost& h, ProjectDocument& d, SessionController& sc, juce::PropertiesFile& s)
    : host (h), doc (d), session (sc), settings (s), meterL (h.engine.outputMeterL), meterR (h.engine.outputMeterR)
{
    setupFader (fader);
    fader.onValueChange = [this] {
        const double db = fader.getValue();
        host.engine.masterGain = db <= -59.95 ? 0.0f : juce::Decibels::decibelsToGain ((float) db);
        settings.setValue ("mainOutDb", db);
    };
    fader.setTooltip ("Main output level (monitoring level; not part of exports)");
    fader.setValue (settings.getDoubleValue ("mainOutDb", 0.0), juce::sendNotificationSync);
    addAndMakeVisible (fader);
    addAndMakeVisible (meterL);
    addAndMakeVisible (meterR);

    viewport.setViewedComponent (&stripHolder, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);

    emptyLabel.setText ("No tracks yet. Click \"+ Track\" (or add tracks in Studio) and they appear here as channel strips.", juce::dontSendNotification);
    addTrackButton.setTooltip ("Add an audio track (Ctrl+T)");
    addTrackButton.onClick = [this] { session.addTrack(); };
    addAndMakeVisible (addTrackButton);
    styleLabel (emptyLabel, 14.0f);
    emptyLabel.setJustificationType (juce::Justification::centred);
    addChildComponent (emptyLabel);
    session.selectionChanged.addChangeListener (this);
    rebind();
}

MixerPage::~MixerPage()
{
    session.selectionChanged.removeChangeListener (this);
    tracksNode.removeListener (this);
}

void MixerPage::rebind()
{
    tracksNode.removeListener (this);
    tracksNode = doc.tracks();
    tracksNode.addListener (this);
    rebuild();
}

void MixerPage::rebuild()
{
    strips.clear();
    int i = 0;
    for (auto t : tracksNode)
    {
        if (! t.hasType (ids::TRACK) || i >= maxTracks) continue;
        auto* s = strips.add (new ChannelStrip (doc, host, session, t, i++));
        stripHolder.addAndMakeVisible (s);
    }
    emptyLabel.setVisible (strips.isEmpty());
    resized();
}

void MixerPage::timerCallback()
{
    meterL.tick();
    meterR.tick();
    for (auto* s : strips)
        s->tick();
}

void MixerPage::paint (juce::Graphics& g)
{
    g.fillAll (palette::bg);
    drawSectionPanel (g, masterBox, "Main out");
    g.setFont (uiFont (10.0f));
    for (int db : { 0, -6, -12, -18, -24, -36, -48, -60 })
    {
        const float n = (db + 60.0f) / 60.0f;
        const int y = scale.getBottom() - juce::roundToInt (n * scale.getHeight());
        g.setColour (palette::textDim);
        g.drawText (juce::String (db), scale.getX(), y - 6, scale.getWidth(), 12, juce::Justification::centredRight);
    }
}

void MixerPage::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto top = r.removeFromTop (30);
    addTrackButton.setBounds (top.removeFromLeft (100));
    r.removeFromTop (8);
    masterBox = r.removeFromRight (150);
    r.removeFromRight (12);
    viewport.setBounds (r);
    emptyLabel.setBounds (r);
    stripHolder.setSize (juce::jmax (r.getWidth(), strips.size() * ChannelStrip::width), r.getHeight() - viewport.getScrollBarThickness());
    for (int i = 0; i < strips.size(); ++i)
        strips[i]->setBounds (i * ChannelStrip::width, 0, ChannelStrip::width, stripHolder.getHeight());

    auto s = masterBox.reduced (12).withTrimmedTop (24).withTrimmedBottom (8);
    auto meters = s.removeFromRight (34);
    meterL.setBounds (meters.removeFromLeft (15).withTrimmedBottom (28));
    meters.removeFromLeft (4);
    meterR.setBounds (meters.removeFromLeft (15).withTrimmedBottom (28));
    scale = s.removeFromRight (28).withTrimmedTop (8).withTrimmedBottom (28);
    fader.setBounds (s);
}
} // namespace ff
