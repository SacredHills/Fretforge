#include "GlobalKeys.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

namespace ff::globalkeys
{
namespace
{
std::function<bool (const juce::KeyPress&)>& handler()
{
    static std::function<bool (const juce::KeyPress&)> h;
    return h;
}

bool isTransportKey (const juce::KeyPress& k)
{
    return k.getKeyCode() == juce::KeyPress::spaceKey && ! k.getModifiers().isAnyModifierKeyDown();
}

/** Controls that would otherwise take Space for themselves. Text fields keep their focus. */
bool shouldNotKeepFocus (juce::Component* c)
{
    return dynamic_cast<juce::Button*> (c) != nullptr || dynamic_cast<juce::Slider*> (c) != nullptr
        || dynamic_cast<juce::ComboBox*> (c) != nullptr;
}

struct FocusRule final : public juce::FocusChangeListener
{
    explicit FocusRule (juce::Component& m) : main (m) {}
    void globalFocusChanged (juce::Component* c) override
    {
        if (c == nullptr || ! shouldNotKeepFocus (c)) return;
        c->setWantsKeyboardFocus (false);
        c->setMouseClickGrabsKeyboardFocus (false);
        if (main.isShowing() && main.getTopLevelComponent() == c->getTopLevelComponent()) main.grabKeyboardFocus();
        else c->giveAwayKeyboardFocus();
    }
    juce::Component& main;
};
std::unique_ptr<FocusRule> focusRule;

#if JUCE_WINDOWS
HHOOK hook = nullptr;

bool isTextControl (HWND w)
{
    wchar_t cls[128] = {};
    if (w == nullptr || GetClassNameW (w, cls, 127) == 0) return false;
    const auto name = juce::String (cls).toLowerCase();
    return name.contains ("edit") || name.contains ("text") || name.contains ("combobox");
}

LRESULT CALLBACK keyHook (int code, WPARAM key, LPARAM flags)
{
    if (code == HC_ACTION && key == VK_SPACE)
    {
        const HWND focus = GetFocus();
        wchar_t cls[128] = {};
        const bool juceWindow = focus != nullptr && GetClassNameW (focus, cls, 127) > 0 && juce::String (cls).startsWith ("JUCE_");
        const bool modifiers = GetKeyState (VK_CONTROL) < 0 || GetKeyState (VK_MENU) < 0 || GetKeyState (VK_SHIFT) < 0;
        // a plugin's own (native) editor has the focus: Space is for the transport, unless it is typing into a field
        if (focus != nullptr && ! juceWindow && ! modifiers && ! isTextControl (focus) && handler())
        {
            const bool down = (flags & (1u << 31)) == 0, repeat = (flags & (1 << 30)) != 0;
            if (down && ! repeat)
                juce::MessageManager::callAsync ([] { forward (juce::KeyPress (juce::KeyPress::spaceKey)); });
            return 1;
        }
    }
    return CallNextHookEx (hook, code, key, flags);
}
#endif
} // namespace

void setHandler (std::function<bool (const juce::KeyPress&)> h) { handler() = std::move (h); }

bool forward (const juce::KeyPress& k)
{
    return isTransportKey (k) && handler() && handler() (k);
}

void install (juce::Component& mainContent)
{
    focusRule = std::make_unique<FocusRule> (mainContent);
    juce::Desktop::getInstance().addFocusChangeListener (focusRule.get());
#if JUCE_WINDOWS
    if (hook == nullptr) hook = SetWindowsHookExW (WH_KEYBOARD, keyHook, nullptr, GetCurrentThreadId());
#endif
}

void uninstall()
{
#if JUCE_WINDOWS
    if (hook != nullptr) UnhookWindowsHookEx (hook);
    hook = nullptr;
#endif
    if (focusRule != nullptr) juce::Desktop::getInstance().removeFocusChangeListener (focusRule.get());
    focusRule.reset();
    handler() = nullptr;
}
} // namespace ff::globalkeys
