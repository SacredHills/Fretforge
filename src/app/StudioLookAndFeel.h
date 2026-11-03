#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace ff
{
namespace palette
{
    inline const juce::Colour bg        { 0xff141619 };
    inline const juce::Colour panel     { 0xff1c1f24 };
    inline const juce::Colour panelHi   { 0xff252930 };
    inline const juce::Colour well      { 0xff0e1013 };
    inline const juce::Colour outline   { 0xff323840 };
    inline const juce::Colour text      { 0xffdde1e6 };
    inline const juce::Colour textDim   { 0xff8b939d };
    inline const juce::Colour accent    { 0xfff0a030 };  // amber
    inline const juce::Colour accentDim { 0xff7a5420 };
    inline const juce::Colour good      { 0xff46c38a };
    inline const juce::Colour warn      { 0xffe8c547 };
    inline const juce::Colour bad       { 0xffe5484d };
    inline const juce::Colour info      { 0xff4c9bf5 };
}

inline juce::Font uiFont (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font monoFont (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height,
                                          bold ? juce::Font::bold : juce::Font::plain));
}

/** "  ·  " separator (built from UTF-8 so debug builds don't assert on non-ASCII literals). */
inline juce::String dotSep() { return juce::String::fromUTF8 ("  \xc2\xb7  "); }

class StudioLookAndFeel : public juce::LookAndFeel_V4
{
public:
    StudioLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return uiFont (14.0f); }
    juce::Font getLabelFont (juce::Label& l) override { return l.getFont(); }
    juce::Font getPopupMenuFont() override { return uiFont (14.0f); }
    int getTabButtonBestWidth (juce::TabBarButton&, int) override { return 130; }
};

/** Panel background with title strip, used by every page section. */
void drawSectionPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title);
} // namespace ff
