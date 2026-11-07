#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

/** Space (play / stop) works whatever window is in front: the main window, the MIDI editor, a tool window or a plugin
    window, including a third-party plugin's own editor (on Windows a thread keyboard hook catches it, except while a
    text field has the focus). Buttons, sliders and menus never keep the keyboard focus, so Space never "clicks" the
    last button you used instead of playing. */
namespace ff::globalkeys
{
/** Set by the main window: handles a key that reached a floating window. Returns true if used. */
void setHandler (std::function<bool (const juce::KeyPress&)>);
/** For floating windows: forwards transport keys (Space) to the handler. */
bool forward (const juce::KeyPress&);
/** Installs the focus rules and (Windows) the keyboard hook. `mainContent` gets the focus back from main-window controls. */
void install (juce::Component& mainContent);
void uninstall();
} // namespace ff::globalkeys
