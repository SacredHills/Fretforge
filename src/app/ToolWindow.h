#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "StudioLookAndFeel.h"

namespace ff
{
/** A floating window (like a plugin window) showing a component that lives elsewhere: closing only hides it,
    and its position and size are remembered in the settings under `key`. */
class ToolWindow : public juce::DocumentWindow
{
public:
    ToolWindow (const juce::String& title, juce::Component& content, int w, int h, juce::PropertiesFile& s, juce::String settingsKey)
        : juce::DocumentWindow (title, palette::bg, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
          settings (s), key (std::move (settingsKey))
    {
        setUsingNativeTitleBar (true);
        setContentNonOwned (&content, false);
        setResizable (true, false);
        setResizeLimits (360, 240, 3000, 2000);
        setAlwaysOnTop (true);
        const auto saved = settings.getValue (key);
        if (saved.isEmpty() || ! restoreWindowStateFromString (saved))
            centreWithSize (w, h);
    }
    ~ToolWindow() override { settings.setValue (key, getWindowStateAsString()); }

    void showFront()
    {
        setVisible (true);
        toFront (true);
    }
    void closeButtonPressed() override
    {
        settings.setValue (key, getWindowStateAsString());
        setVisible (false);
        if (onClosed) onClosed();
    }
    std::function<void()> onClosed;

private:
    juce::PropertiesFile& settings;
    juce::String key;
};
} // namespace ff
