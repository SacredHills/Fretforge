#include "StudioLookAndFeel.h"

namespace ff
{
StudioLookAndFeel::StudioLookAndFeel()
{
    using namespace palette;
    auto scheme = LookAndFeel_V4::ColourScheme (bg, panel, panelHi, outline, text, accent, juce::Colours::black,
                                                panelHi, text);
    setColourScheme (scheme);

    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::DocumentWindow::textColourId, text);
    setColour (juce::TextButton::buttonColourId, panelHi);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::ComboBox::backgroundColourId, well);
    setColour (juce::ComboBox::outlineColourId, outline);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::Label::textColourId, text);
    setColour (juce::TextEditor::backgroundColourId, well);
    setColour (juce::TextEditor::outlineColourId, outline);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId, accent);
    setColour (juce::Slider::thumbColourId, accent);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::Slider::backgroundColourId, well);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxBackgroundColourId, well);
    setColour (juce::Slider::textBoxOutlineColourId, outline);
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, well);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::ScrollBar::thumbColourId, outline);
    setColour (juce::TabbedComponent::backgroundColourId, bg);
    setColour (juce::TabbedComponent::outlineColourId, outline);
    setColour (juce::TabbedButtonBar::tabOutlineColourId, outline);
    setColour (juce::TabbedButtonBar::frontOutlineColourId, accent);
    setColour (juce::ListBox::backgroundColourId, well);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, outline);
    setColour (juce::TooltipWindow::backgroundColourId, panelHi);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, outline);
    setColour (juce::GroupComponent::outlineColourId, outline);
    setColour (juce::GroupComponent::textColourId, textDim);
}

juce::Font StudioLookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return uiFont (juce::jmin (15.0f, h * 0.55f), true);
}

void StudioLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& base,
                                              bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : base;
    if (! b.isEnabled()) c = c.withMultipliedAlpha (0.4f);
    else if (down) c = c.darker (0.25f);
    else if (highlighted) c = c.brighter (0.12f);

    g.setColour (c);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (palette::outline.brighter (b.getToggleState() ? 0.0f : 0.1f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void StudioLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds();
    auto box = r.removeFromLeft (r.getHeight()).reduced (5).toFloat();
    g.setColour (palette::well);
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (highlighted ? palette::accent : palette::outline);
    g.drawRoundedRectangle (box, 3.0f, 1.0f);
    if (b.getToggleState())
    {
        g.setColour (palette::accent);
        g.fillRoundedRectangle (box.reduced (3.5f), 2.0f);
    }
    g.setColour (b.isEnabled() ? palette::text : palette::textDim);
    g.setFont (uiFont (14.0f));
    g.drawFittedText (b.getButtonText(), r.withTrimmedLeft (4), juce::Justification::centredLeft, 1);
}

void StudioLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos,
                                          float maxPos, juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style == juce::Slider::LinearBar || style == juce::Slider::LinearBarVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
        return;
    }

    const bool vertical = s.isVertical();
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float trackW = 4.0f;
    juce::Rectangle<float> track = vertical ? bounds.withSizeKeepingCentre (trackW, bounds.getHeight())
                                            : bounds.withSizeKeepingCentre (bounds.getWidth(), trackW);
    g.setColour (palette::well);
    g.fillRoundedRectangle (track, 2.0f);

    juce::Rectangle<float> filled = vertical ? track.withTop (pos) : track.withRight (pos);
    g.setColour (s.isEnabled() ? palette::accent : palette::textDim);
    g.fillRoundedRectangle (filled, 2.0f);

    juce::Rectangle<float> thumb = vertical ? juce::Rectangle<float> (bounds.getCentreX() - 11.0f, pos - 5.0f, 22.0f, 10.0f)
                                            : juce::Rectangle<float> (pos - 5.0f, bounds.getCentreY() - 9.0f, 10.0f, 18.0f);
    g.setColour (palette::panelHi.brighter (0.3f));
    g.fillRoundedRectangle (thumb, 2.0f);
    g.setColour (palette::accent);
    if (vertical) g.drawHorizontalLine ((int) pos, thumb.getX() + 3, thumb.getRight() - 3);
    else          g.drawVerticalLine ((int) pos, thumb.getY() + 3, thumb.getBottom() - 3);
}

void StudioLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start,
                                          float end, juce::Slider& s)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float radius = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
    const auto c = r.getCentre();
    const float angle = start + pos * (end - start);

    // bipolar controls (pan, +/- gains) light up from the centre, so they don't read like a volume knob
    const bool pan = (bool) s.getProperties()["pan"];
    const bool bipolar = pan || (s.getMinimum() < 0.0 && s.getMaximum() > 0.0
                                 && std::abs (s.getMinimum() + s.getMaximum()) < 1.0e-6 * (s.getMaximum() - s.getMinimum()));
    const float mid = start + 0.5f * (end - start);
    juce::Path bgArc, valueArc;
    bgArc.addCentredArc (c.x, c.y, radius, radius, 0.0f, start, end, true);
    if (bipolar) valueArc.addCentredArc (c.x, c.y, radius, radius, 0.0f, juce::jmin (mid, angle), juce::jmax (mid, angle), true);
    else valueArc.addCentredArc (c.x, c.y, radius, radius, 0.0f, start, angle, true);
    g.setColour (palette::well);
    g.strokePath (bgArc, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (! s.isEnabled() ? palette::textDim : pan ? palette::info : palette::accent);
    g.strokePath (valueArc, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (bipolar)   // centre tick
    {
        g.setColour (palette::textDim);
        const auto a = c.getPointOnCircumference (radius + 3.0f, mid), b = c.getPointOnCircumference (radius - 3.0f, mid);
        g.drawLine ({ a, b }, 1.5f);
    }
    if (pan && radius > 9.0f)
    {
        g.setColour (palette::textDim);
        g.setFont (uiFont (9.0f, true));
        const auto l = c.getPointOnCircumference (radius + 2.0f, start), rr = c.getPointOnCircumference (radius + 2.0f, end);
        g.drawText ("L", juce::Rectangle<float> (10, 10).withCentre (l.translated (-3.0f, 4.0f)), juce::Justification::centred);
        g.drawText ("R", juce::Rectangle<float> (10, 10).withCentre (rr.translated (3.0f, 4.0f)), juce::Justification::centred);
    }

    g.setColour (palette::panelHi);
    g.fillEllipse (r.withSizeKeepingCentre (radius * 1.4f, radius * 1.4f));
    juce::Path p;
    p.addRectangle (-1.5f, -radius * 0.7f, 3.0f, radius * 0.45f);
    g.setColour (palette::text);
    g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c));
}

void StudioLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (box.hasKeyboardFocus (true) ? palette::accent : palette::outline);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (palette::textDim.withAlpha (box.isEnabled() ? 1.0f : 0.4f));
    g.fillPath (arrow);
}

void StudioLookAndFeel::drawTabButton (juce::TabBarButton& b, juce::Graphics& g, bool over, bool)
{
    auto r = b.getLocalBounds();
    const bool front = b.isFrontTab();
    g.setColour (front ? palette::panel : (over ? palette::panelHi : palette::bg));
    g.fillRect (r);
    if (front)
    {
        g.setColour (palette::accent);
        g.fillRect (r.removeFromBottom (2));
    }
    g.setColour (front ? palette::text : palette::textDim);
    g.setFont (uiFont (14.0f, front));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, 1);
}

void drawSectionPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
{
    g.setColour (palette::panel);
    g.fillRoundedRectangle (area.toFloat(), 6.0f);
    g.setColour (palette::outline);
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 6.0f, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour (palette::textDim);
        g.setFont (uiFont (12.0f, true));
        g.drawText (title.toUpperCase(), area.reduced (12, 0).removeFromTop (28), juce::Justification::centredLeft);
    }
}
} // namespace ff
