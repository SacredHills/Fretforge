#include "MilestonePage.h"
#include "../StudioLookAndFeel.h"

namespace ff
{
MilestonePage::MilestonePage (juce::String t, int n, juce::String m, juce::StringArray s, juce::String a)
    : title (std::move (t)), milestone (std::move (m)), note (std::move (a)), number (n), scope (std::move (s))
{
}

void MilestonePage::paint (juce::Graphics& g)
{
    g.fillAll (palette::bg);
    auto r = getLocalBounds().reduced (32, 28);

    g.setColour (palette::text);
    g.setFont (uiFont (26.0f, true));
    g.drawText (title, r.removeFromTop (36), juce::Justification::centredLeft);

    const auto badgeText = juce::String::fromUTF8 ("NOT YET IMPLEMENTED  \xc2\xb7  MILESTONE ") + juce::String (number) + ": "
                           + milestone.toUpperCase();
    const auto badgeFont = uiFont (13.0f, true);
    const int badgeW = (int) juce::GlyphArrangement::getStringWidth (badgeFont, badgeText) + 24;
    auto badge = r.removeFromTop (30).removeFromLeft (badgeW);
    g.setColour (palette::accentDim.withAlpha (0.5f));
    g.fillRoundedRectangle (badge.toFloat().reduced (0, 3), 4.0f);
    g.setColour (palette::accent);
    g.setFont (badgeFont);
    g.drawText (badgeText, badge.reduced (12, 0), juce::Justification::centredLeft);
    r.removeFromTop (18);

    g.setColour (palette::textDim);
    g.setFont (uiFont (14.0f));
    g.drawFittedText ("This section has no working features in this build, so it shows no controls. "
                      "Planned scope for this milestone:", r.removeFromTop (40), juce::Justification::topLeft, 2);

    const int colW = juce::jmax (260, r.getWidth() / 2);
    auto col = r.removeFromLeft (colW);
    const int perCol = (scope.size() + 1) / 2;
    for (int i = 0; i < scope.size(); ++i)
    {
        if (i == perCol) col = r;
        auto row = col.removeFromTop (24);
        g.setColour (palette::accentDim);
        g.fillEllipse (row.removeFromLeft (14).withSizeKeepingCentre (6, 6).toFloat());
        g.setColour (palette::text.withAlpha (0.85f));
        g.setFont (uiFont (14.0f));
        g.drawText (scope[i], row.withTrimmedLeft (6), juce::Justification::centredLeft);
    }

    if (note.isNotEmpty())
    {
        auto n = getLocalBounds().reduced (32, 28).removeFromBottom (60);
        g.setColour (palette::panel);
        g.fillRoundedRectangle (n.toFloat(), 6.0f);
        g.setColour (palette::textDim);
        g.setFont (uiFont (13.0f));
        g.drawFittedText (note, n.reduced (14, 8), juce::Justification::centredLeft, 3);
    }
}
} // namespace ff
