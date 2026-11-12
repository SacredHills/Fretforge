#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "../../Widgets.h"

namespace ff
{
/** A searchable list of the files in a folder (recursively), e.g. NAM captures or cabinet IRs.
    The folder is remembered in the settings under `settingsKey`. Clicking a row calls onPick. */
class FileBrowserList : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
public:
    FileBrowserList (juce::PropertiesFile& s, juce::String settingsKey, juce::String wildcard, juce::String noun)
        : settings (s), key (std::move (settingsKey)), patterns (std::move (wildcard)), what (std::move (noun))
    {
        styleLabel (title, 13.0f, palette::text, true);
        addAndMakeVisible (title);
        folderButton.setButtonText ("Folder...");
        folderButton.setTooltip ("Choose the folder with your " + what);
        folderButton.onClick = [this] { chooseFolder(); };
        addAndMakeVisible (folderButton);
        addAndMakeVisible (extraButton);
        extraButton.setVisible (false);
        refreshButton.setButtonText ("Refresh");
        refreshButton.setTooltip ("Look for new files in the folder (it also happens by itself every few seconds)");
        refreshButton.onClick = [this] { rescan(); };
        addAndMakeVisible (refreshButton);
        search.setTextToShowWhenEmpty ("Search " + what, palette::textDim);
        search.onTextChange = [this] { filter(); };
        addAndMakeVisible (search);
        list.setRowHeight (24);
        list.setColour (juce::ListBox::backgroundColourId, palette::well);
        addAndMakeVisible (list);
        rescan();
        startTimer (6000);
    }

    std::function<void (const juce::File&)> onPick;
    /** Right click on a row (the file); the owner shows the menu. */
    std::function<void (const juce::File&)> onContext;
    juce::File current;                       // highlighted (the loaded file)
    juce::TextButton extraButton, refreshButton;   // extraButton: optional button next to "Folder..." (e.g. TONE3000)

    juce::File folder() const { return juce::File (settings.getValue (key)); }
    void setFolder (const juce::File& f) { settings.setValue (key, f.getFullPathName()); rescan(); }
    void setCurrent (const juce::File& f) { current = f; list.repaint(); }

    void rescan (bool force = true)
    {
        auto oldAll = all;
        all.clear();
        const auto dir = folder();
        if (dir.isDirectory())
        {
            for (const auto& e : juce::RangedDirectoryIterator (dir, true, patterns, juce::File::findFiles))
            {
                all.add (e.getFile());
                if (all.size() >= 5000) break;
            }
            std::sort (all.begin(), all.end(), [&dir] (const juce::File& a, const juce::File& b) {
                return a.getRelativePathFrom (dir).compareNatural (b.getRelativePathFrom (dir)) < 0;
            });
        }
        if (! force && all == oldAll) return;   // nothing new: keep the list (and the scroll position)
        title.setText (dir.isDirectory() ? juce::String (what.substring (0, 1).toUpperCase() + what.substring (1)) + " in " + dir.getFileName()
                                               + "  (" + juce::String (all.size()) + ")"
                                         : "Choose a folder with your " + what,
                       juce::dontSendNotification);
        filter();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (26);
        folderButton.setBounds (top.removeFromRight (90));
        top.removeFromRight (4);
        refreshButton.setBounds (top.removeFromRight (70));
        if (extraButton.isVisible()) { top.removeFromRight (6); extraButton.setBounds (top.removeFromRight (110)); }
        title.setBounds (top);
        r.removeFromTop (4);
        search.setBounds (r.removeFromTop (28));
        r.removeFromTop (4);
        list.setBounds (r);
    }

private:
    juce::PropertiesFile& settings;
    juce::String key, patterns, what;
    juce::Label title;
    juce::TextButton folderButton;
    juce::TextEditor search;
    juce::ListBox list { "files", this };
    juce::Array<juce::File> all, shown;
    std::unique_ptr<juce::FileChooser> chooser;

    void chooseFolder()
    {
        const auto start = folder().isDirectory() ? folder() : juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        chooser = std::make_unique<juce::FileChooser> ("Folder with " + what, start);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult().isDirectory()) setFolder (fc.getResult()); });
    }

    juce::String label (const juce::File& f) const
    {
        const auto dir = folder();
        return f.isAChildOf (dir) ? f.getRelativePathFrom (dir) : f.getFileName();
    }

    void filter()
    {
        shown.clear();
        const auto words = juce::StringArray::fromTokens (search.getText(), " ", "");
        for (auto& f : all)
        {
            const auto text = label (f);
            bool ok = true;
            for (auto& w : words) ok = ok && text.containsIgnoreCase (w);
            if (ok) shown.add (f);
        }
        list.updateContent();
        list.repaint();
    }

    int getNumRows() override { return shown.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
    {
        if (! juce::isPositiveAndBelow (row, shown.size())) return;
        const auto& f = shown.getReference (row);
        const bool isCurrent = f == current;
        if (isCurrent) { g.setColour (palette::accentDim); g.fillRect (0, 0, w, h); }
        g.setColour (isCurrent ? palette::text : palette::textDim);
        g.setFont (uiFont (13.0f, isCurrent));
        g.drawText (label (f), 8, 0, w - 12, h, juce::Justification::centredLeft, true);
    }
    void timerCallback() override { if (isShowing()) rescan (false); }
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (! juce::isPositiveAndBelow (row, shown.size())) return;
        if (e.mods.isPopupMenu()) { if (onContext) onContext (shown[row]); return; }
        if (onPick) onPick (shown[row]);
    }
};
} // namespace ff
