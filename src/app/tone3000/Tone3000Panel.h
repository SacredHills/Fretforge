#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "Tone3000.h"
#include "../Widgets.h"

namespace ff
{
/** Browse and download NAM captures and cabinet IRs from TONE3000, then load them into a rig. */
class Tone3000Panel : public juce::Component, private juce::ChangeListener
{
public:
    explicit Tone3000Panel (t3k::Client&);
    ~Tone3000Panel() override;

    /** cab = IRs (format "ir"), otherwise NAM captures. `load` receives each downloaded file. */
    void setMode (bool cab, const juce::String& targetName, std::function<void (const juce::File&)> load);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    t3k::Client& client;
    bool cabMode = false;
    std::function<void (const juce::File&)> onLoad;

    juce::Label title, account, keyLabel, targetLabel, status, toneTitle, toneInfo, attribution;
    juce::TextEditor keyEditor, searchBox;
    juce::TextButton signButton, browseButton { "Browse TONE3000..." }, refreshButton { "Refresh" }, prevButton { "<" }, nextButton { ">" };
    juce::TextButton downloadButton { "Download && load" }, openPageButton { "Open on tone3000.com" }, keyHelpButton { "Get a key..." };
    juce::ComboBox feedCombo;

    struct ToneListModel : juce::ListBoxModel
    {
        Tone3000Panel& o;
        explicit ToneListModel (Tone3000Panel& p) : o (p) {}
        int getNumRows() override { return o.shownTones.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
        void selectedRowsChanged (int row) override { o.selectTone (row); }
    } toneModel { *this };
    struct ModelListModel : juce::ListBoxModel
    {
        Tone3000Panel& o;
        explicit ModelListModel (Tone3000Panel& p) : o (p) {}
        int getNumRows() override { return o.models.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { o.downloadRow (row); }
    } modelModel { *this };
    juce::ListBox toneList { "tones", &toneModel }, modelList { "models", &modelModel };

    juce::Array<t3k::Tone> tones, shownTones;
    juce::Array<t3k::Model> models;
    t3k::Tone currentTone;
    int page = 1, totalPages = 1, requestId = 0;

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refreshAccount(); }
    void refreshAccount();
    void reload();
    void selectTone (int row);
    void showTone (const t3k::Tone&);
    void downloadRow (int row);
    void setStatus (const juce::String& s, bool error = false);
    juce::String format() const { return cabMode ? "ir" : "nam"; }
};
} // namespace ff
