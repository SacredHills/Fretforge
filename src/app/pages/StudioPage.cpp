#include "StudioPage.h"
#include "core/MusicTheory.h"

namespace ff
{
using namespace project;

ProjectSheet::ProjectSheet (ProjectDocument& d)
    : doc (d)
{
    nameLabel.setText ("Project name", juce::dontSendNotification);
    keyLabel.setText ("Key", juce::dontSendNotification);
    notesLabel.setText ("Song notes", juce::dontSendNotification);
    for (auto* l : { &nameLabel, &keyLabel, &notesLabel })
    {
        styleLabel (*l, 12.0f, palette::textDim, true);
        addAndMakeVisible (l);
    }
    styleLabel (infoLabel, 13.0f);
    infoLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (infoLabel);

    nameEditor.setFont (uiFont (18.0f, true));
    nameEditor.onReturnKey = [this] { commitName(); };
    nameEditor.onFocusLost = [this] { commitName(); };
    addAndMakeVisible (nameEditor);

    int id = 1;
    for (const char* mode : { "major", "minor" })
        for (int pc = 0; pc < 12; ++pc)
            keyCombo.addItem (theory::pitchClassName (pc, pc == 3 || pc == 5 || pc == 8 || pc == 10) + " " + mode, id++);
    keyCombo.onChange = [this] {
        if (! updating)
        {
            doc.undo().beginNewTransaction ("Key");
            doc.set (doc.transport(), ids::keySignature, keyCombo.getText());
        }
    };
    addAndMakeVisible (keyCombo);

    notesEditor.setMultiLine (true, true);
    notesEditor.setReturnKeyStartsNewLine (true);
    notesEditor.setFont (uiFont (14.0f));
    notesEditor.setTextToShowWhenEmpty ("Lyrics ideas, structure, gear and tone notes...", palette::textDim);
    notesEditor.onFocusLost = [this] { commitNotes(); };
    notesEditor.onTextChange = [this] { if (! updating) commitNotes(); };
    addAndMakeVisible (notesEditor);

    rebind();
}

ProjectSheet::~ProjectSheet() { root.removeListener (this); }

void ProjectSheet::rebind()
{
    root.removeListener (this);
    root = doc.tree();
    root.addListener (this);
    refresh();
}

void ProjectSheet::refresh()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    if (! nameEditor.hasKeyboardFocus (true))
        nameEditor.setText (root[ids::name].toString(), false);
    if (notesEditor.getText() != root[ids::notes].toString())
        notesEditor.setText (root[ids::notes].toString(), false);

    const auto key = doc.transport()[ids::keySignature].toString();
    for (int i = 0; i < keyCombo.getNumItems(); ++i)
        if (keyCombo.getItemText (i) == key)
            keyCombo.setSelectedItemIndex (i, juce::dontSendNotification);
    refreshInfo();
}

void ProjectSheet::refreshInfo()
{
    auto fmt = [] (const juce::var& iso) {
        const auto t = juce::Time::fromISO8601 (iso.toString());
        return t == juce::Time() ? juce::String ("-") : t.formatted ("%Y-%m-%d %H:%M");
    };
    juce::String info;
    info << "Location:   " << (doc.hasLocation() ? doc.getBundle().getFullPathName() : juce::String ("not saved yet")) << "\n"
         << "Created:    " << fmt (root[ids::created]) << "\n"
         << "Last saved: " << (doc.hasLocation() ? fmt (root[ids::modified]) : juce::String ("-")) << "\n"
         << "Status:     " << (doc.isDirty() ? "unsaved changes" : "saved");
    if (doc.lastAutosave() != juce::Time())
        info << "  (autosaved " << doc.lastAutosave().formatted ("%H:%M:%S") << ")";
    infoLabel.setText (info, juce::dontSendNotification);
}

void ProjectSheet::commitName()
{
    const auto text = nameEditor.getText().trim();
    if (text.isNotEmpty() && text != root[ids::name].toString())
    {
        doc.undo().beginNewTransaction ("Rename project");
        doc.set (root, ids::name, text);
    }
}

void ProjectSheet::commitNotes()
{
    if (notesEditor.getText() != root[ids::notes].toString())
    {
        const juce::ScopedValueSetter<bool> svs (updating, true);
        if (doc.undo().getCurrentTransactionName() != "Edit notes")
            doc.undo().beginNewTransaction ("Edit notes");
        doc.set (root, ids::notes, notesEditor.getText()); // keystrokes coalesce into one undo step
    }
}

void ProjectSheet::paint (juce::Graphics& g)
{
    g.fillAll (palette::bg);
    drawSectionPanel (g, sheetBox, "Project");
}

void ProjectSheet::resized()
{
    auto r = getLocalBounds().reduced (16);
    sheetBox = r;

    auto s = sheetBox.reduced (16).withTrimmedTop (20);
    auto left = s.removeFromLeft (juce::jmin (420, s.getWidth() / 2));
    s.removeFromLeft (20);
    nameLabel.setBounds (left.removeFromTop (20));
    nameEditor.setBounds (left.removeFromTop (34));
    left.removeFromTop (12);
    keyLabel.setBounds (left.removeFromTop (20));
    keyCombo.setBounds (left.removeFromTop (28).withWidth (200));
    left.removeFromTop (16);
    infoLabel.setBounds (left.removeFromTop (90));

    notesLabel.setBounds (s.removeFromTop (20));
    notesEditor.setBounds (s);
}
} // namespace ff
