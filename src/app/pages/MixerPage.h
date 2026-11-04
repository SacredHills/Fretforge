#pragma once
#include "../EngineHost.h"
#include "../ProjectDocument.h"
#include "../SessionController.h"
#include "../Widgets.h"

namespace ff
{
/** A strip's insert plugins: one row per plugin (click: plugin window, right-click: bypass / move / replace / remove)
    and a "+ Plugin" row. */
class InsertList : public juce::Component, public juce::SettableTooltipClient, public juce::DragAndDropTarget
{
public:
    InsertList (SessionController& s, juce::ValueTree t) : session (s), track (t) {}
    int rowsWanted() const;
    static constexpr int rowHeight = 20;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    // drag a plugin onto another track (copies it with its settings) or up / down in this list (moves it)
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails& d) override { dropRow = juce::jlimit (0, rig::numInserts (track), (d.localPosition.y + rowHeight / 2) / rowHeight); repaint(); }
    void itemDragExit (const SourceDetails&) override { dropRow = -1; repaint(); }
    void itemDropped (const SourceDetails&) override;
    juce::ValueTree getTrack() const { return track; }
private:
    SessionController& session;
    juce::ValueTree track;
    bool dragging = false;
    int dropRow = -1;
    bool onBypassButton (int x) const { return x > getWidth() - 18; }
};

/** A strip's sends (drag up / down for the level, click for options) and its output (click to route). */
class RouteList : public juce::Component, public juce::SettableTooltipClient
{
public:
    RouteList (SessionController& s, ProjectDocument& d, juce::ValueTree t) : session (s), doc (d), track (t) {}
    int rowsWanted() const;
    static constexpr int rowHeight = 20;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    SessionController& session;
    ProjectDocument& doc;
    juce::ValueTree track;
    juce::ValueTree dragSend;
    double dragStartLevel = 0.0;
    bool dragged = false;
    juce::ValueTree sendAt (int row) const;
};

/** One mixer channel strip bound to a TRACK node; meter reads the engine's track meter. */
class ChannelStrip : public juce::Component, private juce::ValueTree::Listener, private juce::ChangeListener,
                     public juce::DragAndDropTarget
{
public:
    bool isInterestedInDragSource (const SourceDetails& d) override { return insertList.isInterestedInDragSource (d); }
    void itemDropped (const SourceDetails& d) override { insertList.itemDropped (d); }
    ChannelStrip (ProjectDocument&, EngineHost&, SessionController&, juce::ValueTree track, int slot);
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    ~ChannelStrip() override;
    void rename() { nameLabel.showEditor(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();
    static constexpr int width = 104;

private:
    ProjectDocument& doc;
    EngineHost& host;
    SessionController& session;
    juce::ValueTree track;
    int slot;
    juce::Slider fader, pan;
    juce::TextButton mute { "M" }, solo { "S" }, arm { "R" }, ampButton { "NAM AMP" };
    PowerButton ampPower;
    juce::Label nameLabel;
    InsertList insertList { session, track };
    RouteList routeList { session, doc, track };
    void showMenu();
    float level = -100.0f, hold = -100.0f;
    int holdTicks = 0;
    bool updating = false;
    void refresh();
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { refresh(); resized(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { refresh(); resized(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { refresh(); resized(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { insertList.repaint(); }
};

/** Mixer: a channel strip per track plus the main output. */
class MixerPage : public juce::Component, public juce::DragAndDropContainer, private juce::Timer, private juce::ValueTree::Listener, private juce::ChangeListener
{
public:
    MixerPage (EngineHost&, ProjectDocument&, SessionController&, juce::PropertiesFile&);
    ~MixerPage() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void rebind();

private:
    EngineHost& host;
    ProjectDocument& doc;
    SessionController& session;
    juce::PropertiesFile& settings;
    juce::ValueTree tracksNode;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { for (auto* s : strips) s->repaint(); }
    juce::Viewport viewport;
    juce::Component stripHolder;
    juce::OwnedArray<ChannelStrip> strips;
    juce::Slider fader;
    MeterView meterL, meterR;
    juce::Rectangle<int> masterBox, scale;
    juce::Label emptyLabel;
    juce::TextButton addTrackButton { "+ Track" };
    std::unique_ptr<InsertList> masterInserts;   // plugins on the master bus
    int masterRows = 0;

    void rebuild();
    void timerCallback() override;
    void visibilityChanged() override { if (isShowing()) startTimerHz (30); else stopTimer(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& c) override { if (c.hasType ("TRACK")) rebuild(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& c, int) override { if (c.hasType ("TRACK")) rebuild(); }
    void valueTreeChildOrderChanged (juce::ValueTree& p, int, int) override { if (p == tracksNode) rebuild(); }
};
} // namespace ff
