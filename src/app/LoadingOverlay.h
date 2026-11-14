#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Widgets.h"

namespace ff
{
/** Covers the window while a project loads: what is still loading, a progress bar, and it takes the clicks, so
    nothing is pressed while plugins and samples come in. Goes away by itself when everything has loaded. */
class LoadingOverlay : public juce::Component, private juce::Timer
{
public:
    LoadingOverlay()
    {
        setInterceptsMouseClicks (true, true);
        hide.setButtonText ("Hide");
        hide.setTooltip ("Close this and keep working while the rest loads");
        hide.onClick = [this] { finish(); };
        addAndMakeVisible (hide);
        setVisible (false);
    }

    /** What is still loading (message thread). */
    std::function<juce::StringArray()> source;

    void begin (const juce::String& heading)
    {
        title = heading;
        peak = 0;
        quiet = 0;
        current.clear();
        shownAt = juce::Time::getMillisecondCounter();
        items = source ? source() : juce::StringArray();
        setVisible (true);
        toFront (false);
        startTimer (100);
        refreshNow();
    }

    /** Updates and paints at once (used between plugin loads, when timers and paints would otherwise wait). */
    void refreshNow()
    {
        if (! isVisible()) return;
        update();
        repaint();
        if (auto* p = getPeer()) p->performAnyPendingRepaintsNow();
    }

    void setCurrent (const juce::String& what) { current = what; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.62f));
        const auto card = cardBounds();
        g.setColour (palette::panel);
        g.fillRoundedRectangle (card.toFloat(), 8.0f);
        g.setColour (palette::outline);
        g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 8.0f, 1.0f);
        auto r = card.reduced (22, 18);
        g.setColour (palette::text);
        g.setFont (uiFont (17.0f, true));
        g.drawText (title, r.removeFromTop (26), juce::Justification::centredLeft, true);
        r.removeFromTop (6);
        auto bar = r.removeFromTop (10).toFloat();
        g.setColour (palette::well);
        g.fillRoundedRectangle (bar, 4.0f);
        const float frac = peak > 0 ? juce::jlimit (0.0f, 1.0f, (float) (peak - items.size()) / (float) peak) : 0.0f;
        g.setColour (palette::accent);
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (8.0f, bar.getWidth() * frac)), 4.0f);
        r.removeFromTop (10);
        g.setColour (palette::textDim);
        g.setFont (uiFont (12.5f));
        const int total = juce::jmax (peak, 1), done = juce::jmax (0, peak - items.size());
        g.drawText (peak > 0 ? juce::String (done) + " of " + juce::String (total) + " loaded" : juce::String ("Getting ready..."),
                    r.removeFromTop (18), juce::Justification::centredLeft, true);
        if (current.isNotEmpty())
        {
            g.setColour (palette::text);
            g.drawText ("Now: " + current, r.removeFromTop (18), juce::Justification::centredLeft, true);
        }
        g.setColour (palette::textDim);
        for (int i = 0; i < juce::jmin (4, items.size()); ++i)
            g.drawText ("- " + items[i], r.removeFromTop (18), juce::Justification::centredLeft, true);
        if (items.size() > 4) g.drawText ("and " + juce::String (items.size() - 4) + " more", r.removeFromTop (18), juce::Justification::centredLeft, true);
        g.setFont (uiFont (11.5f));
        g.drawText ("Big plugins can pause the window for a moment while they load. That is normal - please wait.",
                    juce::Rectangle<int> (card.getX() + 22, card.getBottom() - 32, card.getWidth() - 44 - 80, 18), juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        const auto card = cardBounds();
        hide.setBounds (card.getRight() - 22 - 70, card.getBottom() - 34, 70, 24);
    }

private:
    juce::Rectangle<int> cardBounds() const
    {
        return getLocalBounds().withSizeKeepingCentre (juce::jmin (560, getWidth() - 40), 190 + 18 * juce::jmin (5, items.size()));
    }
    juce::String title, current;
    juce::StringArray items;
    juce::TextButton hide;
    int peak = 0, quiet = 0;
    juce::uint32 shownAt = 0;

    void update()
    {
        items = source ? source() : juce::StringArray();
        peak = juce::jmax (peak, items.size());
        resized();
    }

    void finish()
    {
        stopTimer();
        setVisible (false);
    }

    void timerCallback() override
    {
        update();
        quiet = items.isEmpty() ? quiet + 1 : 0;
        const bool minimumShown = juce::Time::getMillisecondCounter() - shownAt > 600;
        if (quiet >= 4 && minimumShown) { finish(); return; }
        repaint();
    }
};
}
