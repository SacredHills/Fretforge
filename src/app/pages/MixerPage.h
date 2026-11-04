#pragma once
#include "../EngineHost.h"
#include "../ProjectDocument.h"
#include "../SessionController.h"
#include "../Widgets.h"

namespace ff
{
/** A strip's insert plugins: one row per plugin (click: plugin window, right-click: bypass / move / replace / remove)
    and a "+ Plugin" row. */
class InsertList : public juce::Component, public juce::SettableTooltipClient
{
public:
    InsertList (SessionController& s, juce::ValueTree t) : session (s), track (t) {}
    int rowsWanted() const;
    static constexpr int rowHeight = 20;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    SessionController& session;
    juce::ValueTree track;
};

/** One mixer channel strip bound to a TRACK node; meter reads the engine's track meter. */
class ChannelStrip : public juce::Component, private juce::ValueTree::Listener, private juce::ChangeListener
{
public:
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
    juce::Label nameLabel;
    InsertList insertList { session, track };
    void showMenu();
    float level = -100.0f, hold = -100.0f;
    int holdTicks = 0;
    bool updating = false;
    void refresh();
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { refresh(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { refresh(); resized(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { refresh(); resized(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { insertList.repaint(); }
};

/** Mixer: a channel strip per track plus the main output. */
class MixerPage : public juce::Component, private juce::Timer, private juce::ValueTree::Listener, private juce::ChangeListener
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

    void rebuild();
    void timerCallback() override;
    void visibilityChanged() override { if (isShowing()) startTimerHz (30); else stopTimer(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& c) override { if (c.hasType ("TRACK")) rebuild(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& c, int) override { if (c.hasType ("TRACK")) rebuild(); }
    void valueTreeChildOrderChanged (juce::ValueTree& p, int, int) override { if (p == tracksNode) rebuild(); }
};
} // namespace ff
