#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "StudioLookAndFeel.h"
#include "GlobalKeys.h"

namespace ff
{
/** Plugin and tool windows are ordinary windows: they sit above the main window while you use them and go behind it
    (or behind any other program) when you click there. Nothing is forced always-on-top any more: that made them cover
    other windows. Kept as a type so the windows that own one need no change. */
class FloatAboveApp
{
public:
    explicit FloatAboveApp (juce::DocumentWindow&) {}
};

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
    bool keyPressed (const juce::KeyPress& k) override { return globalkeys::forward (k) || juce::DocumentWindow::keyPressed (k); }

private:
    juce::PropertiesFile& settings;
    juce::String key;
    FloatAboveApp floating { *this };
};
} // namespace ff
