#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "StudioLookAndFeel.h"
#include "engine/AudioEngine.h"
#include <cmath>

namespace ff
{
inline float gainToDb (float g) { return g > 1.0e-6f ? 20.0f * std::log10 (g) : -120.0f; }

/** Vertical or horizontal meter fed from a real engine LevelMeter. Shows peak with
    decay, peak-hold line and a clip LED (click to reset). Call tick() at ~30-60 Hz. */
class MeterView : public juce::Component, public juce::SettableTooltipClient
{
public:
    MeterView (LevelMeter& m, bool isVertical = true) : meter (m), vertical (isVertical) {}

    void tick()
    {
        const float pk = meter.uiPeak;
        const float db = gainToDb (pk);
        level = db > level ? db : juce::jmax (db, level - 1.6f); // ~50 dB/s at 30 Hz
        if (db >= hold) { hold = db; holdTicks = 45; }
        else if (--holdTicks <= 0) hold = juce::jmax (-60.0f, hold - 1.5f);
        clipped = meter.clipped.load();
        repaint();
    }

    void mouseDown (const juce::MouseEvent&) override { meter.clipped = false; hold = -60.0f; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        auto led = vertical ? r.removeFromTop (6) : r.removeFromRight (6);
        r = vertical ? r.withTrimmedTop (2) : r.withTrimmedRight (2);

        g.setColour (clipped ? palette::bad : palette::well);
        g.fillRect (led);

        g.setColour (palette::well);
        g.fillRect (r);

        auto norm = [] (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
        const float n = norm (level);
        auto filled = vertical ? r.withTop (r.getBottom() - juce::roundToInt (n * r.getHeight()))
                               : r.withWidth (juce::roundToInt (n * r.getWidth()));
        juce::ColourGradient grad = vertical
            ? juce::ColourGradient (palette::good, 0.0f, (float) r.getBottom(), palette::bad, 0.0f, (float) r.getY(), false)
            : juce::ColourGradient (palette::good, (float) r.getX(), 0.0f, palette::bad, (float) r.getRight(), 0.0f, false);
        grad.addColour (0.75, palette::good);
        grad.addColour (0.9, palette::warn);
        g.setGradientFill (grad);
        g.fillRect (filled);

        if (hold > -59.0f)
        {
            g.setColour (palette::text);
            const float h = norm (hold);
            if (vertical) g.fillRect (r.getX(), r.getBottom() - juce::roundToInt (h * r.getHeight()), r.getWidth(), 1);
            else          g.fillRect (r.getX() + juce::roundToInt (h * r.getWidth()), r.getY(), 1, r.getHeight());
        }
    }

    float levelDb() const { return level; }

private:
    LevelMeter& meter;
    bool vertical;
    float level = -120.0f, hold = -60.0f;
    int holdTicks = 0;
    bool clipped = false;
};

/** Small label helper with consistent styling. */
inline void styleLabel (juce::Label& l, float size = 13.0f, juce::Colour c = palette::textDim, bool bold = false)
{
    l.setFont (uiFont (size, bold));
    l.setColour (juce::Label::textColourId, c);
}

/** A heading + body text card describing work that is not implemented yet. No controls. */
class NoticeCard : public juce::Component
{
public:
    NoticeCard (juce::String heading, juce::String body) : title (std::move (heading)), text (std::move (body)) {}
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (palette::panel);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (palette::accentDim);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);
        r.reduce (16, 12);
        g.setColour (palette::accent);
        g.setFont (uiFont (14.0f, true));
        g.drawText (title, r.removeFromTop (22), juce::Justification::centredLeft);
        g.setColour (palette::textDim);
        g.setFont (uiFont (13.5f));
        g.drawFittedText (text, r, juce::Justification::topLeft, 12, 1.0f);
    }
private:
    juce::String title, text;
};
} // namespace ff
