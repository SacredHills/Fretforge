#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "StudioLookAndFeel.h"
#include "GlobalKeys.h"

namespace ff
{
/** Keeps a floating window (plugin editor, tool window) above RAVENYR's own windows only while RAVENYR is the active
    app: when you switch to a browser or another program the window drops back instead of covering it. It also drops
    back while any dialog is open. */
class FloatAboveApp : private juce::Timer
{
public:
    explicit FloatAboveApp (juce::DocumentWindow& w) : window (w)
    {
       #if ! JUCE_LINUX   // X11 cannot change "always on top" in place (JUCE would recreate the window), so Linux leaves it to the window manager
        timerCallback();
        startTimerHz (5);
       #endif
    }

private:
    juce::DocumentWindow& window;
    void timerCallback() override
    {
        // ...and never while a dialog is up (file chooser, alert, the save prompt): an always-on-top plugin window
        // would cover it, and the app would look frozen while it waits for a dialog nobody can see
        const bool dialogUp = juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0;
        const bool front = juce::Process::isForegroundProcess() && ! dialogUp;
        if (front != window.isAlwaysOnTop()) window.setAlwaysOnTop (front);
    }
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
