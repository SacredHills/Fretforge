#pragma once
#include "../ProjectDocument.h"
#include "../Widgets.h"
#include "arrange/ArrangeView.h"

namespace ff
{
/** Project sheet: name, key, song notes and file info. */
class ProjectSheet : public juce::Component, private juce::ValueTree::Listener
{
public:
    explicit ProjectSheet (ProjectDocument&);
    ~ProjectSheet() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void rebind();
    void refreshInfo();

private:
    ProjectDocument& doc;
    juce::ValueTree root;
    juce::Label nameLabel, keyLabel, notesLabel, infoLabel;
    juce::TextEditor nameEditor, notesEditor;
    juce::ComboBox keyCombo;
    juce::Rectangle<int> sheetBox;
    bool updating = false;

    void commitName();
    void commitNotes();
    void refresh();
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { refresh(); }
};

/** Studio section: the arrange window (tracks, clips, recording) and the project sheet. */
class StudioPage : public juce::Component
{
public:
    StudioPage (ProjectDocument& d, EngineHost& h, SessionController& s, juce::PropertiesFile& p)
        : arrange (d, h, s, p), sheet (d)
    {
        tabs.setTabBarDepth (30);
        tabs.setOutline (0);
        tabs.addTab ("Arrange", palette::bg, &arrange, false);
        tabs.addTab ("Project", palette::bg, &sheet, false);
        addAndMakeVisible (tabs);
    }
    void resized() override { tabs.setBounds (getLocalBounds()); }
    void rebind() { arrange.rebind(); sheet.rebind(); }
    void refreshInfo() { sheet.refreshInfo(); }
    void showArrange() { tabs.setCurrentTabIndex (0); }

    ArrangeView arrange;
    ProjectSheet sheet;

private:
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
};
} // namespace ff
